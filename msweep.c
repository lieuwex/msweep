// msweep: Simple terminal minesweeper
// original by tomsmeding (http://www.tomsmeding.com)
// fork by lieuwex (http://www.lieuwe.xyz)

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

inline void bel() {
	prflush("\x07");
}

typedef enum Direction {
	UP,
	RIGHT,
	DOWN,
	LEFT
} Direction;

struct termios tios_bak;

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

void endscreen(void) {
	tcsetattr(0, TCSAFLUSH, &tios_bak);
	prflush("\x1B[?1049l");
}

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

void getkey(Key *key) {
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

void board_destroy(Board *bd) {
	free(bd->data);
	free(bd);
}

void board_goto(Board *bd, int x, int y) {
	(void)bd;
	gotoxy(2 + 2*x, 1 + y);
}

void board_gotocursor(Board *bd) {
	board_goto(bd, bd->curx, bd->cury);
}

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

// doesn't gotoxy
void board_drawcell(Board *bd, int x, int y) {
	const Data *data = bd->data + (bd->w*y + x);
	if (data->flag) putchar('#');
	// else if(data->bomb)putchar(','); // DEBUG
	else if (!data->open) putchar('.');
	else if (data->count==0) putchar(' ');
	else putchar('0'+data->count);
}

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

void board_flag(Board *bd) {
	Data *data = bd->data+(bd->w*bd->cury + bd->curx);
	if (data->open) {
		bel();
		return;
	}
	data->flag = !data->flag;
	bd->nflags += 2*data->flag - 1;
}

void board_flood(Board *bd, int x, int y) {
	bd->data[bd->w*y + x].open = true;
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

void board_gen(Board *bd, int x, int y) {
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

bool board_open(Board *bd) {
	Data *data = bd->data + (bd->w*bd->cury + bd->curx);
	if (bd->startTime == -1) {
		board_gen(bd, bd->curx, bd->cury);
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

bool board_win(Board *bd) {
	return bd->startTime != -1 && bd->nopen == bd->w*bd->h - bd->nbombs;
}

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


void prompt_quit(int height) {
	if (prompt("Really quit?", height)) {
		exit(0);
	} else {
		prflush("\x1B[2K");
	}
}

bool prompt_playagain(const char *msg, int height) {
	char *str;
	asprintf(&str, "\x1B[7m%s\x1B[0m\nPlay again?", msg);
	return prompt(str, height);
}

void signalend(int sig) {
	(void)sig;
	endscreen();
	exit(1);
}

void formatTime(char **dest, time_t seconds) {
	int minutes = seconds / 60;
	int hours = minutes / 60;
	seconds = seconds % 60;
	asprintf(dest, "%02d:%02d:%02ld", hours, minutes, seconds);
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

			char *msg;
			asprintf(&msg, "You win! (%s)", timestamp);

			if (!prompt_playagain(msg, height + 2)) {
				break;
			}

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
				if (!prompt_playagain("BOOM!", height + 2)) {
					quit = true;
					break;
				}
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
