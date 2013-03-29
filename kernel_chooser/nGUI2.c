#include <curses.h>
#include <menu.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <wait.h>

#include "common3.h"
#include "menu3.h"
#include "nGUI2.h"

int	menu_i, menu_sizex, // size fo the menu_window ( getmaxyx does not work )
		menu_sizey,
		entries_count; // count of created entries ( useful for error handling )
ITEM **items, **items2; // ncurses menu items
MENU *menu[2]; //, *menu2; // ncurses menu
WINDOW 	*menu_window, // ncurses menu window
				*messages_win; // ncurses messages window
char 	**local_entries; // our padded copy of the items names

struct _default_entries
{
	const char *name;
	const int num;
};

const struct _default_entries default_entries[] =
{
	{
		.name = "reboot",
		.num = MENU_REBOOT
	},
	{
		.name = "shutdown",
		.num = MENU_HALT
	},
	{
		.name = "recovery",
		.num = MENU_RECOVERY
	}
#ifdef SHELL
	,
	{
		.name = "emergency shell",
		.num = MENU_SHELL
	}
#endif
};

void print_in_middle(WINDOW *win, int starty, int startx, int width, char *string)
{
	int length, x, y;
	float temp;

	if(win == NULL)
		win = stdscr;
	getyx(win, y, x);
	if(startx != 0)
		x = startx;
	if(starty != 0)
		y = starty;
	if(width == 0)
		width = 80;

	length = strlen(string);
	temp = (width - length)/ 2;
	x = startx + (int)temp;
	mvwprintw(win, y, x, "%s", string);
	refresh();
}

/** copy src string to *dst, padding equally from right and left
 * @dest: destination char *
 * @src: source char *
 * @sizex: size of the padded string
*/
int copy_with_padd(char **dest, int sizex, char *src)
{
	int len;

	len = strlen(src);
	*dest = malloc((sizex+1)*sizeof(char));
	if(!*dest)
	{
		FATAL("malloc - %s\n",strerror(errno));
		return -1;
	}
	memset(*dest,' ',sizex);
	strncpy(*dest+1,src,len);
	*((*dest)+sizex)='\0';
	return 0;
}

void create_box(int h, int w, int y, int x)
{
	int i;
	for(i = y; i <= y + h; i++)
		mvhline(i,x,' ',w);
	mvaddch(y, x, ACS_ULCORNER);
	mvaddch(y, x + w, ACS_URCORNER);
	mvaddch(y + h, x, ACS_LLCORNER);
	mvaddch(y + h, x + w, ACS_LRCORNER);
	mvhline(y, x + 1, ACS_HLINE, w - 1);
	mvhline(y + h, x + 1, ACS_HLINE, w - 1);
	mvvline(y + 1, x, ACS_VLINE, h - 1);
	mvvline(y + 1, x + w, ACS_VLINE, h - 1);
	refresh();
}

int nc_init(void)
{
	int sizex,sizey;

	/* Initialize variables to free in case of errors */
	menu[0] = NULL;
	menu[1] = NULL;
	menu_i = 0;
	messages_win = menu_window = NULL;
	items = NULL;
	local_entries = NULL;
	entries_count = 0;

	/* Initialize curses */
	initscr(); // TODO: error checking
	cbreak();
	noecho();
	curs_set(0);
	keypad(stdscr, TRUE);

	/* Initialize colors */
	start_color();
	init_pair(COLOR_LOG_DEBUG, COLOR_CYAN, COLOR_BLACK);
	init_pair(COLOR_LOG_WARN, COLOR_YELLOW, COLOR_BLACK);
	init_pair(COLOR_LOG_ERROR, COLOR_RED, COLOR_BLACK);
	init_pair(COLOR_MENU_BORDER, COLOR_BLACK, COLOR_BLUE);
	init_pair(COLOR_MENU_TEXT, COLOR_WHITE, COLOR_BLUE);
	init_pair(COLOR_MENU_TITLE, COLOR_CYAN, COLOR_BLUE);
	init_pair(COLOR_POPUP, COLOR_WHITE, COLOR_RED);

	/* Print title */
	mvprintw(0,0,"%s",HEADER_LEFT);
	mvprintw(0,COLS-strlen(HEADER_RIGHT),"%s",HEADER_RIGHT);

	/* Create messages window */
	sizey = (LINES * MSG_HEIGHT_PERC)/100;
	sizex = (COLS * MSG_WIDTH_PERC)/100;
	mvhline(LINES-sizey+1,0,ACS_HLINE,sizex);
	messages_win = newwin(sizey-1,sizex,(LINES-sizey)+2,0);
	scrollok(messages_win,TRUE);

	refresh();
	return 0;
}

void nc_destroy_menu(void)
{
	unpost_menu(menu[0]);
	unpost_menu(menu[1]);
	free_menu(menu[0]);
	free_menu(menu[1]);
	delwin(menu_window);
	if(items[entries_count]) // items are NULL-terminated
		free_item(items[entries_count]);
	while(entries_count--)
	{
		free(local_entries[entries_count]);
		free_item(items[entries_count]);
	}
	free(items);
	free(local_entries);
}

void nc_destroy(void)
{
	/*delwin(messages_win);
	 * WARNING: call this function here causes a kernel panic.
	 * NOTE:DEBUG("messages_win=%p",messages_win) return a valid pointer...
	 * we have to find why this happens
	 * NOTE: btw, kernel saying that all memory has freed...strange behaviour.
	 */
	clear();
	endwin();
}

/* save ncurses state
   used to temporarly leave ncusrses (run external program) */
void nc_save() {
	unpost_menu(menu[menu_i]); // will repost when we get back
	def_prog_mode();
	endwin();
}

/* restore curses state */
void nc_load() {
	reset_prog_mode();
	keypad(stdscr, true);
	refresh();	
}

void draw_menu_border(void)
{
	int startx,starty,len;
	/* Print a border around the main window and print a title */
	getbegyx(menu_window,starty,startx);
	startx -= 1;
	starty -= 3;
	len = strlen(PROMPT);
	create_box(menu_sizey+3,menu_sizex+1,starty,startx);
	mvhline(starty+2, startx+1, ACS_HLINE, menu_sizex);
	mvaddch(starty+2,startx,ACS_LTEE);
	mvaddch(starty+2,startx+menu_sizex+1,ACS_RTEE);
	attron(COLOR_PAIR(COLOR_MENU_TITLE));
	mvprintw(starty+1,startx + ((menu_sizex - len)/2),"%s",PROMPT);
	attroff(COLOR_PAIR(COLOR_MENU_TITLE));
	wrefresh(menu_window);
	refresh();
}

int nc_compute_menu(menu_entry *list)
{
	int n_choices, default_count;
	menu_entry *current;

	/* Compute menu size */
	menu_sizey = (LINES * MENU_HEIGHT_PERC)/100;
	menu_sizex = (COLS * MENU_WIDTH_PERC)/100;

	menu_sizex-=2; // borders
	menu_sizey-=5; // borders + head

	/* Create items */
	// count entries
	for(n_choices=0,current=list;current;current=current->next,n_choices++);
	// count the default ones
	default_count = ARRAY_SIZE(default_entries);
	// sum
	items = (ITEM **)malloc((n_choices+1)*sizeof(ITEM *));
	items2 = (ITEM **)malloc((default_count+1)*sizeof(ITEM *));
	local_entries = malloc((n_choices+default_count+1)*sizeof(char *));
	if( !local_entries) //!items ||
	{
		FATAL("malloc - %s\n",strerror(errno));
		goto error;
	}
	// create default entries
	for(entries_count=0;entries_count<default_count;entries_count++)
		if(copy_with_padd(local_entries+entries_count,menu_sizex,(char*)default_entries[entries_count].name))
			goto error;
		else
		{
			// HACK: store src pointer in item description
			items2[entries_count] = new_item(local_entries[entries_count], default_entries[entries_count].name);
			if(!items2[entries_count])
			{
				free(local_entries[entries_count]);
				FATAL("new_item - %s\n",strerror(errno));
				goto error;
			}
		}
	for(current=list;current;current=current->next,entries_count++)
		if(copy_with_padd(local_entries+entries_count,menu_sizex,current->name))
			goto error;
		else
		{
			// HACK: store src pointer in item description
			items[entries_count-default_count] = new_item(local_entries[entries_count], current->name);
			if(!items[entries_count-default_count])
			{
				free(local_entries[entries_count]);
				FATAL("new_item - %s\n",strerror(errno));
				goto error;
			}
		}
	items[entries_count-default_count] = new_item(NULL,NULL);
	items2[default_count] = new_item(NULL,NULL);
	/* Create menu */
	menu[0] = new_menu((ITEM **)items);
	menu[1] = new_menu((ITEM **)items2);
	if(!menu[0] || !menu[1])
	{
		FATAL("new_menu - %s\n",strerror(errno));
		goto error;
	}

	/* Create the window to be associated with the menu */
	menu_window = newwin( menu_sizey, menu_sizex, 5, (COLS-menu_sizex)/2);
	if(!menu_window)
	{
		FATAL("newwin - %s\n",strerror(errno));
		goto error;
	}
	keypad(menu_window, TRUE);
	if(wattron(menu_window,COLOR_PAIR(COLOR_MENU_TEXT))==ERR)
		ERROR("wattron - %s\n",strerror(errno));
	/* Set main window and sub window */
	
	// hacky temp variable
	for (n_choices=0; n_choices<2; n_choices++)
	{	
		
		/* Set menu option not to show the description */
		menu_opts_off(menu[n_choices], O_SHOWDESC);
		/* Change item color */
		set_menu_back(menu[n_choices], COLOR_PAIR(COLOR_MENU_TEXT));

		if(set_menu_win(menu[n_choices], menu_window)==ERR)
		{
			FATAL("set_menu_win - %s\n",strerror(errno));
			goto error;
		}
		// set menu size
		if(set_menu_format(menu[n_choices], menu_sizey,1) != E_OK)
			ERROR("set_menu_format - %s\n",strerror(errno));

		/* Set menu mark  */
		if(set_menu_mark(menu[n_choices], NULL)!= E_OK)
			ERROR("set_menu_mark - %s\n",strerror(errno));
	}

	wbkgd(menu_window,COLOR_PAIR(COLOR_MENU_TEXT));
	attron(COLOR_PAIR(COLOR_MENU_BORDER));
	draw_menu_border();
	attroff(COLOR_PAIR(COLOR_MENU_BORDER));

	attron(COLOR_PAIR(COLOR_LOG_ERROR));
	mvprintw(7+menu_sizey,(COLS-ARRAY_SIZE(HELP_MESSAGE))/2,HELP_MESSAGE);
	attroff(COLOR_PAIR(COLOR_LOG_ERROR));

	return 0;
	error:
	nc_destroy_menu();
	return -1;
}

void nc_help_popup()
{
	int x, y, i;
	const char *strings[] = HELP_PAGE;
	x = (COLS-menu_sizex)/2+1;
	y = 3;

	unpost_menu(menu[menu_i]); // E_POSTED from nc_get_user_choice
	attron(COLOR_PAIR(COLOR_POPUP));
	create_box( menu_sizey+3, menu_sizex+1, y-1, x-2);
	for(i=0;strings[i];i++)
	{
		mvprintw(y+i,x,"%s",strings[i]);
	}
	mvprintw(y+i+1,(COLS-strlen(PRESS_ENTER))/2,PRESS_ENTER);
	
	nc_wait_enter();
	attron(COLOR_PAIR(COLOR_MENU_BORDER));
	draw_menu_border();
}

int nc_get_user_choice()
{
	int c;

	post_menu:

	/* Post the menu */
	if(post_menu(menu[menu_i])!=E_OK)
	{
		ERROR("post_menu - %s\n",strerror(errno));
		goto error;
	}
	wrefresh(menu_window);
	wrefresh(messages_win);
	refresh();

	while((c = wgetch(menu_window)) != 10)
	{
		DEBUG("key %i (%c)\n", c, c);
		switch(c)
		{
			case 278:
			case KEY_DOWN:
				menu_driver(menu[menu_i], REQ_DOWN_ITEM);
				break;
			//case KEY_VOLUMEUP: TODO
			case KEY_UP:
				menu_driver(menu[menu_i], REQ_UP_ITEM);
				break;
			case KEY_NPAGE:
				menu_driver(menu[menu_i], REQ_SCR_DPAGE);
				break;
			case KEY_PPAGE:
				menu_driver(menu[menu_i], REQ_SCR_UPAGE);
				break;
			case HELP_KEY:
				nc_help_popup();
				goto post_menu;
			case 'r':
				unpost_menu(menu[menu_i]);
				menu_i = (menu_i==0)?1:0;
				goto post_menu;
		}
		wrefresh(menu_window);
	}

	c = item_index(current_item(menu[menu_i]));
	if (menu_i == 0)
	{
		if (c == 0)
			c = MENU_DEFAULT;
		else
			c += 1;
		return c;
	}
	else if (menu_i == 1)
	{
		return default_entries[c].num;
	}

	error:
	return MENU_FATAL_ERROR;
}

void nc_push_message(int i, char *prefix, char *fmt,...)
{
	va_list ap;

	if(!messages_win)
		return;

	va_start(ap,fmt);
	wattron(messages_win, COLOR_PAIR(i));
	wprintw(messages_win,prefix);
	wattroff(messages_win, COLOR_PAIR(i));
	vwprintw(messages_win,fmt,ap);
	wrefresh(messages_win);
	va_end(ap);
}

void nc_wait_enter(void)
{
	while(getch() != 10);
}

/** wait for a keypress while coutdown.
 * if user press something return 0.
 * -1 otherwise
 */
int nc_wait_for_keypress(void)
{
	int x,y,timeout;

	y = (LINES/2)-1;
	x = (COLS - snprintf(NULL,0,WAIT_MESSAGE,0))/2;

	timeout(1000); // wait one second between keypress checks

	for (timeout=TIMEOUT_BOOT; timeout>0; timeout--) {
		mvprintw(y,x,WAIT_MESSAGE, timeout);
		refresh();
		if (getch() != ERR) {
			mvprintw(y,x,"%*s",COLS-x-1," ");
			return 0;
		}
	}
	return -1;
}