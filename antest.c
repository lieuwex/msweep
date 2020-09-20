#include <stdio.h>
#include "analysis/analysis.h"


// static const int init_cells[64] = {
// 	 0,  0,  0,  1,  2, -1, -1, -1,
// 	 0,  0,  0,  1, -1, -1, -1, -1,
// 	 0,  0,  0,  1,  3, -1, -1, -1,
// 	 0,  0,  0,  0,  1, -1, -1, -1,
// 	 0,  0,  1,  1,  2, -1, -1, -1,
// 	 1,  2,  2, -1,  2, -1, -1, -1,
// 	-1, -1, -1, -1, -1, -1, -1, -1,
// 	-1, -1, -1, -1, -1, -1, -1, -1,
// };

static const int init_cells[64] = {
	 2, -1,  1,  0,  1, -1, -1, -1,
	-1,  2,  1,  0,  1, -1, -1, -1,
	 1,  1,  0,  0,  1, -1, -1, -1,
	 0,  0,  0,  0,  1, -1, -1, -1,
	 1,  2,  3,  2,  2,  1,  2, -1,
	 1, -1, -1, -1,  1,  0,  1, -1,
	 1,  3, -1,  3,  1,  0,  1, -1,
	 0,  1, -1,  1,  0,  0,  1, -1,
};

int main(void){
	const int SIZE = 8;
	struct an_board *bd = an_board_alloc(SIZE, SIZE, 10);
	for (int i = 0; i < 64; i++) bd->cells[i].count = init_cells[i];

	struct an_results *res = an_analyse(bd);
	if (!res) {
		printf("Board inconsistent!\n");
		return 1;
	}
	for (int y = 0; y < SIZE; y++) {
		for (int x = 0; x < SIZE; x++) {
			const struct an_cell_info *info = &res->infos[SIZE * y + x];
			switch (info->bomb_certainty) {
				case AN_NO:
					if (info->count != -1) printf("%d ", info->count);
					else printf("- ");
					break;
				case AN_MAYBE: printf("? "); break;
				case AN_CERTAIN: printf("# "); break;
			}
		}
		putchar('\n');
	}
}
