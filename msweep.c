// msweep: Simple terminal minesweeper
// original by tomsmeding (http://www.tomsmeding.com)
// fork by lieuwex (http://www.lieuwe.xyz)

#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>
#include <sys/time.h>
#include <termios.h>
#include <signal.h>
#include <time.h>

#define DEF_WIDTH (9)
#define DEF_HEIGHT (9)

#define prflush(...) do { printf(__VA_ARGS__); fflush(stdout); } while(0)

typedef enum Color {
	C_DEFAULT,
	C_RED,
	C_YELLOW,
	C_GREEN,
} Color;

// colorize the given string, color is reset at the end
static char *colorize(const char *string, Color color) {
	const char *color_code = "";
	switch (color) {
	case C_DEFAULT:
		color_code = "\x1B[0m";
		break;
	case C_RED:
		color_code = "\x1B[31m";
		break;
	case C_YELLOW:
		color_code = "\x1B[33m";
		break;
	case C_GREEN:
		color_code = "\x1B[32m";
		break;
	}

	char *buf;
	asprintf(&buf, "%s%s\x1B[0m", color_code, string);
	return buf;
}

// audible bel
static void bel() {
	prflush("\x07");
}

typedef enum Direction {
	UP,
	RIGHT,
	DOWN,
	LEFT
} Direction;

struct termios tios_bak;

// init the terminal screen with correct settings
void initscreen(void) {
	struct termios tios;
	tcgetattr(0, &tios_bak);
	tios=tios_bak;
	tios.c_lflag&=~(
		ECHO|ECHOE // no echo of normal characters, erasing
#ifdef ECHOKE
		|ECHOKE // ...and killing
#endif
#ifdef ECHOCTL
		|ECHOCTL // don't visibly echo control characters (^V etc.)
#endif
		|ECHONL // don't even echo a newline
		|ICANON // disable canonical mode
#ifdef NOKERNINFO
		|NOKERNINFO // don't print a status line on ^T
#endif
		|IEXTEN // don't handle things like ^V specially
		//|ISIG // disable ^C ^\ and ^Z
		);
	tios.c_cc[VMIN]=1; // read one char at a time
	tios.c_cc[VTIME]=0; // no timeout on reading, make it a blocking read
	tcsetattr(0, TCSAFLUSH, &tios);

	prflush("\x1B[?1049h\x1B[2J\x1B[H");
}

// reset the terminal screen to the original settings before initscreen was
// called
void endscreen(void) {
	tcsetattr(0, TCSAFLUSH, &tios_bak);
	prflush("\x1B[?1049l");
}

// move the cursor to the given (x,y) on the screen (0-indexed)
void gotoxy(int x, int y) {
	prflush("\x1B[%d;%dH", y+1, x+1);
}

typedef enum Keytype {
	KARROW,
	KCHAR,
	KNUM
} Keytype;

typedef struct Key {
	Keytype type;
	char ch;
	Direction dir;
	unsigned short num;
} Key;

// get the key, putting the values in the given key, should be non-NULL.
void getkey(Key *key) {
	memset(key, 0, sizeof(Key));

	char c = getchar();
	if (c >= 48 && c <= 57) {
		key->type = KNUM;
		key->num = c - 48;
		return;
	} else if (c != 27) {
		key->type = KARROW;
		switch (c) {
		case 'h': key->dir = LEFT; return;
		case 'j': key->dir = DOWN; return;
		case 'k': key->dir = UP; return;
		case 'l': key->dir = RIGHT; return;
		default:
			key->type = KCHAR;
			key->ch = c;
			return;
		}
	}
	c = getchar();
	if(c != '[') {
		ungetc(c, stdin);
		key->type = KCHAR;
		key->ch = c;
		return;
	}
	c=getchar();
	switch (c) {
	case 'A': key->type=KARROW; key->dir = UP; return;
	case 'B': key->type=KARROW; key->dir = DOWN; return;
	case 'C': key->type=KARROW; key->dir = RIGHT; return;
	case 'D': key->type=KARROW; key->dir = LEFT; return;
	default:
		// unknown escape code
		while(c < 64 || c > 126) {
			c = getchar();
		}
		getkey(key); // just try again
		return;
	}
}

typedef struct Data{
	bool open, bomb, flag;
	int count;
} Data;

// init the given Data instance
void data_init(Data *data) {
	data->open = false;
	data->bomb = false;
	data->flag = false;
	data->count = 0;
}

typedef struct Board{
	int w,h;
	Data *data;
	int curx,cury;
	int nbombs,nflags,nopen;
	time_t startTime;
} Board;

// allocate a new board with the given width, height, and number of bombs
Board* board_make(int w, int h, int nbombs) {
	Board *bd = (Board *) malloc(sizeof(Board));
	bd->w = w;
	bd->h = h;
	bd->data = (Data *) malloc(w * h * sizeof(Data));
	bd->curx = 0;
	bd->cury = 0;
	bd->nbombs = nbombs;
	bd->nflags = 0;
	bd->nopen = 0;
	bd->startTime = -1;

	for (int i = 0; i < w*h; i++) {
		data_init(bd->data + i);
	}
	return bd;
}

// destroy the given board, freeing all data
void board_destroy(Board *bd) {
	free(bd->data);
	free(bd);
}

// move the cursor to the given (x,y) on the board
void board_goto(Board *bd, int x, int y) {
	(void)bd;
	gotoxy(2 + 2*x, 1 + y);
}

// move the cursor on screen to the current cursor location in state
void board_gotocursor(Board *bd) {
	board_goto(bd, bd->curx, bd->cury);
}

// move cursor `ntimes` steps in the given direction `dir`
void board_shiftcursor(Board *bd, Direction dir, int ntimes) {
	for (int i = 0; i < ntimes; i++) {
		switch(dir) {
		case UP: if (bd->cury>0) bd->cury--; else i = ntimes; break;
		case RIGHT: if (bd->curx<bd->w-1) bd->curx++; else i = ntimes; break;
		case DOWN: if (bd->cury<bd->h-1) bd->cury++; else i = ntimes; break;
		case LEFT: if (bd->curx>0) bd->curx--; else i = ntimes; break;
		}
	}
	board_gotocursor(bd);
}

// draw the cell info at x,y on the terminal
// doesn't gotoxy
void board_drawcell(Board *bd, int x, int y) {
	const Data *data = bd->data + (bd->w*y + x);

	char c;
	Color color = C_DEFAULT;

	if (data->flag) {
		c = '#';
	//} else if (data->bomb) { // DEBUG
	//	putchar(',');
	//	return;
	} else if (!data->open) {
		putchar('.');
	} else if (data->count==0) {
		putchar(' ');
	} else {
		c = '0'+data->count;

		if (data->count == 1) {
			color = C_GREEN;
		} else if (data->count <= 4) {
			color = C_YELLOW;
		} else {
			color = C_RED;
		}
	}

	char arr[] = { c, '\0' };
	char *str = colorize(arr, color);
	printf("%s", str);
	free(str);
}

// draw the board on screen
void board_draw(Board *bd) {
	gotoxy(0, 0);
	putchar('+');
	for (int x = 0; x < bd->w; x++) printf("--");
	printf("-+\n");
	for (int y = 0; y < bd->h; y++) {
		putchar('|');
		for(int x = 0; x < bd->w; x++) {
			putchar(' ');
			board_drawcell(bd, x, y);
		}
		printf(" |");
		switch(y) {
		case 1: printf("   %dx%d minesweeper", bd->w, bd->h); break;
		case 2: printf("   %d bombs", bd->nbombs); break;
		case 3: printf("   %d flag%s placed ", bd->nflags, bd->nflags==1?"":"s"); break;
		case 5: printf("   'f' to flag, <space> to open"); break;
		case 6: printf("   arrow keys to move, 'r' to restart"); break;
		case 7: printf("   'q' to quit"); break;
		}
		putchar('\n');
	}
	putchar('+');
	for (int x=0;x<bd->w;x++) printf("--");
	printf("-+\n");
	board_gotocursor(bd);
}

// set a flag at the current board location
void board_flag(Board *bd) {
	Data *data = bd->data+(bd->w*bd->cury + bd->curx);
	if (data->open) {
		bel();
		return;
	}
	data->flag = !data->flag;
	bd->nflags += 2*data->flag - 1;
}

// flood the board at the given (x,y)
void board_flood(Board *bd, int x, int y) {
	bd->data[bd->w*y + x].open = true;
	bd->data[bd->w*y + x].flag = false;
	bd->nopen++;
	if (bd->data[bd->w*y + x].count != 0) {
		return;
	}
	if (x > 0) {
		Data *d = bd->data+(bd->w*y+x-1);
		if (!d->open) board_flood(bd, x-1, y);
	}
	if (y > 0) {
		Data *d = bd->data+(bd->w*(y-1)+x);
		if (!d->open) board_flood(bd, x, y-1);
	}
	if (x < bd->w-1) {
		Data *d = bd->data+(bd->w*y+x+1);
		if (!d->open) board_flood(bd, x+1, y);
	}
	if (y < bd->h-1) {
		Data *d = bd->data+(bd->w*(y+1)+x);
		if (!d->open) board_flood(bd, x, y+1);
	}
}

// fill the given board, should be empty.
// no bomb is placed on the given (x,y)
void board_fill(Board *bd, int x, int y) {
	int w = bd->w;
	int h = bd->h;
	int chosenpos = (bd->w*y + x);

	int n = bd->nbombs;
	while (n > 0) {
		int pos = rand() % (w * h);
		if (pos == chosenpos || bd->data[pos].bomb) {
			continue;
		}
		bd->data[pos].bomb = true;
		bd->data[pos].count = 0;
		n--;
		for (int dy = pos<w?0:-1; dy<=(pos>=w*(h-1)?0:1); dy++)  {
			for (int dx = pos%w==0?0:-1; dx<=(pos%w==w-1?0:1); dx++) {
				if (dx == 0 && dy == 0) continue;
				if (!bd->data[pos+w*dy+dx].bomb) bd->data[pos+w*dy+dx].count++;
			}
		}
	}

	bd->startTime = time(NULL);
}

// opens the cell at the current cursor position
bool board_open(Board *bd) {
	Data *data = bd->data + (bd->w*bd->cury + bd->curx);
	if (bd->startTime == -1) {
		board_fill(bd, bd->curx, bd->cury);
	}

	if (data->flag || data->open) {
		bel();
		return false;
	} else if (data->bomb) {
		return true;
	}
	board_flood(bd, bd->curx, bd->cury);
	return false;
}

// reveal all bombs
void board_revealbombs(Board *bd) {
	printf("\x1B[7m");
	for(int y=0;y<bd->h;y++)
	for(int x=0;x<bd->w;x++) {
		Data *data=bd->data+(bd->w*y+x);
		if(!data->bomb)continue;
		board_goto(bd, x, y);
		board_drawcell(bd, x, y);
	}
	printf("\x1B[0m");
	board_gotocursor(bd);
}

// check if the player has won
bool board_win(Board *bd) {
	return bd->startTime != -1 && bd->nopen == bd->w*bd->h - bd->nbombs;
}

// prompt the user (yes / no)
bool prompt(const char *msg, int height) {
	gotoxy(0, height);
	prflush("%s [y/N] ", msg);

	bool res;
	Key key;
	while (true) {
		getkey(&key);

		if (key.ch == 'n' || key.ch == 10) {
			res = false;
			break;
		} else if (key.ch == 'y') {
			res = true;
			break;
		}
	}
	prflush("\x1B[2K\x1B[A\x1B[2K");
	return res;
}


// prompt the user and quit if they want to
void prompt_quit(int height) {
	if (prompt("Really quit?", height)) {
		exit(0);
	} else {
		prflush("\x1B[2K");
	}
}

// prompt the player if they want to replay
// shows the given timestamp
bool prompt_playagain(const char *msg, const char *timestamp, int height) {
	char *str;
	asprintf(&str, "\x1B[7m%s (%s)\x1B[0m\nPlay again?", timestamp, msg);
	bool res = prompt(str, height);
	free(str);
	return res;
}

// signal catcher
void signalend(int sig) {
	(void)sig;
	endscreen();
	exit(1);
}

// format the given time_t in a human friendly format
// allocates memory
void formatTime(char **dest, time_t seconds) {
	int minutes = seconds / 60;
	int hours = minutes / 60;
	seconds = seconds % 60;

	if (hours > 0) {
		asprintf(dest, "%02d:%02d:%02ld", hours, minutes, seconds);
	} else {
		asprintf(dest, "%02d:%02ld", minutes, seconds);
	}
}

int main(int argc, char **argv) {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	srand(tv.tv_sec*1000000ULL + tv.tv_usec);

	if (argc >= 2 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
		fprintf(stderr, "Usage: %s [width] [height] [nbombs]\n", argv[0]);
		return 1;
	}

	int width = DEF_WIDTH;
	int height = DEF_HEIGHT;
	int nbombs = -1;
	for (int i = 1; i < argc; i++) {
		int val = atoi(argv[i]);
		switch (i) {
		case 1: width = val; break;
		case 2: height = val; break;
		case 3: nbombs = val; break;
		}
	}
	if (nbombs == -1) {
		nbombs = .123 * width * height;
	}

	if (nbombs >= width*height) {
		fprintf(stderr, "nbombs (=%d) more than or equal to width * height (=%d)\n", nbombs, width*height);
		return 1;
	}

	initscreen();
	atexit(endscreen);
	signal(SIGINT, signalend);

	Board *bd=board_make(width, height, nbombs);
	Key key;
	bool quit = false;
	int repeat = 1;
	bool have_repeat_num = false;
	while (!quit) {
		board_draw(bd);
		if (board_win(bd)) {
			char *timestamp;
			formatTime(&timestamp, time(NULL) - bd->startTime);

			if (!prompt_playagain("You win!", timestamp, height + 2)) {
				break;
			}

			free(timestamp);
			board_destroy(bd);
			bd = board_make(width, height, nbombs);
			continue;
		}
		getkey(&key);
		switch (key.type) {
		case KNUM:
			if (have_repeat_num) {
				if (repeat >= (INT_MAX - key.num) / 10) {  // would overflow
					bel();
					repeat = 1;
					have_repeat_num = false;
				} else {
					repeat = 10 * repeat + key.num;
				}
			} else if (key.num >= 1) {
				repeat = key.num;
				have_repeat_num = true;
			}
			break;
		case KARROW:
			board_shiftcursor(bd, key.dir, repeat);
			repeat = 1;
			have_repeat_num = false;
			break;
		case KCHAR:
			switch (key.ch) {
			case 'q':
				prompt_quit(height + 2);
				break;
			case 'f':
				board_flag(bd);
				break;
			case 'r':
				board_destroy(bd);
				bd=board_make(width, height, nbombs);
				break;
			case ' ':
				if (!board_open(bd)) break;
				board_revealbombs(bd);
				char *timestamp;
				formatTime(&timestamp, time(NULL) - bd->startTime);
				if (!prompt_playagain("BOOM!", timestamp, height + 2)) {
					quit = true;
					break;
				}
				free(timestamp);
				board_destroy(bd);
				bd = board_make(width, height, nbombs);
				break;
			}
			if (have_repeat_num) bel();
			repeat = 1;
			have_repeat_num = false;
			break;
		default:
			bel();
			if (have_repeat_num) bel();
			repeat = 1;
			have_repeat_num = false;
			break;
		}
	}

	board_destroy(bd);
}
