#ifndef GAME_H
#define GAME_H

#define MIN_LEN		3
#define MAX_LEN		12
#define MAX_TRIES	(MAX_LEN + 4)

enum { S_NONE, S_ABSENT, S_PRESENT, S_CORRECT };	/* ordered by rank */
enum { G_PLAY, G_WON, G_LOST };

struct game {
	int len, tries, nguess, state;
	char answer[MAX_LEN + 1];
	char guess[MAX_TRIES][MAX_LEN + 1];
	unsigned char score[MAX_TRIES][MAX_LEN];
	unsigned char key[26];		/* best score seen per letter */
};

int len_ok(int len);
int game_tries(int len);
void score_guess(const char *ans, const char *g, int len, unsigned char *out);
int game_init(struct game *g, const char *answer, int tries);
int game_submit(struct game *g, const char *guess);

#endif
