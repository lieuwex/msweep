#pragma once

#include <stdbool.h>


struct an_cell {
	int count;  // -1 if (unknown == cell not open)
};

struct an_board {
	int w, h;
	int total_bombs;
	struct an_cell *cells;  // [w*h]
};

enum an_certainty {
	AN_NO,
	AN_MAYBE,
	AN_CERTAIN,
};

struct an_cell_info {
	int count;  // -1 if (unknown == cell not open)
	enum an_certainty bomb_certainty;
};

struct an_results {
	const struct an_board *bd;  // not owned
	struct an_cell_info *infos;  // [w*h]
	int *visit_order;
};

struct an_board* an_board_alloc(int w, int h, int total_bombs);
void an_board_destroy(struct an_board *bd);
void an_results_destroy(struct an_results *results);

// Returns NULL if the baord is inconsistent
struct an_results* an_analyse(const struct an_board *bd);
