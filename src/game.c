#include <string.h>
#include "game.h"

static int li(int c)
{
	return c >= 'a' && c <= 'z' ? c - 'a' : -1;
}

static int word_ok(const char *w, int len)
{
	int i;

	if (!len_ok(len) || strlen(w) != (size_t)len)
		return 0;
	for (i = 0; i < len; i++)
		if (li(w[i]) < 0)
			return 0;
	return 1;
}

int len_ok(int len)
{
	return len >= MIN_LEN && len <= MAX_LEN;
}

int game_tries(int len)
{
	return len + 1 < 6 ? 6 : len + 1;
}

void score_guess(const char *ans, const char *g, int len, unsigned char *out)
{
	int left[26] = { 0 };
	int i, k;

	/* greens first; tally the answer letters they did not use up */
	for (i = 0; i < len; i++) {
		if (g[i] == ans[i]) {
			out[i] = S_CORRECT;
		} else {
			out[i] = S_ABSENT;
			if ((k = li(ans[i])) >= 0)
				left[k]++;
		}
	}
	/* a non-green letter is yellow only while the tally lasts */
	for (i = 0; i < len; i++) {
		if (out[i] != S_ABSENT)
			continue;
		k = li(g[i]);
		if (k >= 0 && left[k] > 0) {
			out[i] = S_PRESENT;
			left[k]--;
		}
	}
}

int game_init(struct game *g, const char *answer, int tries)
{
	size_t n = strlen(answer);

	if (n < MIN_LEN || n > MAX_LEN || !word_ok(answer, (int)n))
		return -1;
	if (tries < 1 || tries > MAX_TRIES)
		return -1;
	memset(g, 0, sizeof *g);
	g->len = (int)n;
	g->tries = tries;
	memcpy(g->answer, answer, n + 1);
	return 0;
}

int game_submit(struct game *g, const char *w)
{
	unsigned char *sc;
	int i, k, won = 1;

	if (g->state != G_PLAY || !word_ok(w, g->len))
		return -1;
	sc = g->score[g->nguess];
	memcpy(g->guess[g->nguess], w, (size_t)g->len + 1);
	score_guess(g->answer, w, g->len, sc);
	for (i = 0; i < g->len; i++) {
		k = li(w[i]);
		if (sc[i] > g->key[k])
			g->key[k] = sc[i];
		if (sc[i] != S_CORRECT)
			won = 0;
	}
	g->nguess++;
	if (won)
		g->state = G_WON;
	else if (g->nguess >= g->tries)
		g->state = G_LOST;
	return 0;
}
