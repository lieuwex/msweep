#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "analysis.h"


// #define DBG(...) fprintf(stderr, "an_analyse: " __VA_ARGS__)
#define DBG(...)


struct an_board* an_board_alloc(int w, int h, int total_bombs) {
	struct an_board *bd = malloc(sizeof(struct an_board));
	bd->w = w;
	bd->h = h;
	bd->total_bombs = total_bombs;
	bd->cells = malloc(w * h * sizeof(struct an_cell));
	for (int i = 0; i < w * h; i++) {
		bd->cells[i].count = -1;
	}
	return bd;
}

void an_board_destroy(struct an_board *bd) {
	free(bd->cells);
	free(bd);
}

void an_results_destroy(struct an_results *results) {
	free(results->infos);
	free(results->visit_order);
	free(results);
}


static bool check_consistent(struct an_results *res) {
	const struct an_board *bd = res->bd;
	int num_bombs = 0;
	for (int i = 0; i < bd->w * bd->h; i++) {
		num_bombs += res->infos[i].bomb_certainty == AN_CERTAIN;
	}
	if (num_bombs > bd->total_bombs) return false;

	for (int ii = 0; ii < bd->w * bd->h; ii++) {
		int i = res->visit_order[ii];
		int x = i % bd->w, y = i / bd->w;
		if (res->infos[bd->w * y + x].count == -1) continue;

		int surr_low = 0, surr_high = 0;

		for (int dy = -1; dy <= 1; dy++) {
			if (y + dy < 0 || y + dy >= bd->h) continue;

			for (int dx = -1; dx <= 1; dx++) {
				if (dx == 0 && dy == 0) continue;
				if (x + dx < 0 || x + dx >= bd->w) continue;

				enum an_certainty cert;
				if (res->infos[bd->w * (y + dy) + x + dx].count != -1) {
					cert = res->infos[bd->w * (y + dy) + x + dx].bomb_certainty;
				} else {
					cert = AN_MAYBE;
				}

				switch (res->infos[bd->w * (y + dy) + x + dx].bomb_certainty) {
					case AN_NO: break;
					case AN_MAYBE: surr_high++; break;
					case AN_CERTAIN: surr_low++; surr_high++; break;
				}
			}
		}

		if (surr_low > res->infos[bd->w * y + x].count ||
				surr_high < res->infos[bd->w * y + x].count) {
			return false;
		}
	}

	return true;
}

static bool can_be_consistent_recur(struct an_results *res, int ati) {
	const struct an_board *bd = res->bd;
	if (ati >= bd->w * bd->h) return true;
	int at = res->visit_order[ati];
	if (!check_consistent(res)) return false;

	if (res->infos[at].count != -1 || res->infos[at].bomb_certainty != AN_MAYBE) {
		return can_be_consistent_recur(res, ati + 1);
	}

	res->infos[at].bomb_certainty = AN_CERTAIN;
	bool yes_cons = can_be_consistent_recur(res, ati + 1);
	res->infos[at].bomb_certainty = AN_MAYBE;
	if (yes_cons) return true;

	res->infos[at].bomb_certainty = AN_NO;
	bool no_cons = can_be_consistent_recur(res, ati + 1);
	res->infos[at].bomb_certainty = AN_MAYBE;
	if (no_cons) return true;

	return false;
}

static bool can_be_consistent(struct an_results *res) {
	return can_be_consistent_recur(res, 0);
}

static bool sanity_check(const struct an_results *res) {
	const struct an_board *bd = res->bd;
	for (int i = 0; i < bd->w * bd->h; i++) {
		if (res->infos[i].count != -1 && res->infos[i].bomb_certainty != AN_NO) {
			return false;
		}
	}
	return true;
}

struct an_results* an_analyse(const struct an_board *bd) {
	struct an_results *res = malloc(sizeof(struct an_results));
	res->bd = bd;
	res->infos = malloc(bd->w * bd->h * sizeof(struct an_cell_info));

	bool *next_to_open = calloc(bd->w * bd->h, sizeof(bool));

	for (int i = 0; i < bd->w * bd->h; i++) {
		res->infos[i].count = bd->cells[i].count;
		res->infos[i].bomb_certainty = res->infos[i].count != -1 ? AN_NO : AN_MAYBE;

		if (res->infos[i].count != -1) {
			int x = i % bd->w, y = i / bd->w;
			for (int dy = -1; dy <= 1; dy++) {
				if (y + dy < 0 || y + dy >= bd->h) continue;
				for (int dx = -1; dx <= 1; dx++) {
					if (dx == 0 && dy == 0) continue;
					if (x + dx < 0 || x + dx >= bd->w) continue;
					next_to_open[bd->w * (y + dy) + x + dx] = true;
				}
			}
		}
	}

	res->visit_order = malloc(bd->w * bd->h * sizeof(int));
	int vcurleft = 0, vcurright = bd->w * bd->h - 1;
	for (int i = 0; i < bd->w * bd->h; i++) {
		if (next_to_open[i]) res->visit_order[vcurleft++] = i;
		else res->visit_order[vcurright--] = i;
	}
	assert(vcurleft == vcurright + 1);
	free(next_to_open);

	for (int ii = 0; ii < bd->w * bd->h; ii++) {
		int i = res->visit_order[ii];
		if (res->infos[i].count != -1) continue;

		res->infos[i].bomb_certainty = AN_CERTAIN;
		bool yes_cons = can_be_consistent(res);
		res->infos[i].bomb_certainty = AN_NO;
		bool no_cons = can_be_consistent(res);
		DBG("i=%d yes=%c no=%c\n", i, "nY"[no_cons], "nY"[yes_cons]);
		if (no_cons) {
			if (yes_cons) res->infos[i].bomb_certainty = AN_MAYBE;
			else res->infos[i].bomb_certainty = AN_NO;
		} else {
			if (yes_cons) res->infos[i].bomb_certainty = AN_CERTAIN;
			else {
				DBG("i=%d yes-no both inconsistent\n", i);
				// inconsistent!
				an_results_destroy(res);
				return NULL;
			}
		}
	}

	assert(sanity_check(res));

	return res;
}
