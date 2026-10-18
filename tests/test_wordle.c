/* no curses needed: tests game.c, dict.c, rng.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/dict.h"
#include "../src/game.h"
#include "../src/rng.h"

static int runs, fails;

#define CHECK(c) do { \
	runs++; \
	if (!(c)) { fails++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } \
} while (0)

/* want: G green, Y yellow, . gray */
static void score(const char *ans, const char *g, const char *want)
{
	unsigned char sc[MAX_LEN];
	char got[MAX_LEN + 1];
	int i, n = (int)strlen(ans);

	score_guess(ans, g, n, sc);
	for (i = 0; i < n; i++)
		got[i] = sc[i] == S_CORRECT ? 'G' : sc[i] == S_PRESENT ? 'Y' : '.';
	got[n] = '\0';
	if (strcmp(got, want) != 0)
		fprintf(stderr, "  %s vs %s: got %s want %s\n", ans, g, got, want);
	CHECK(strcmp(got, want) == 0);
}

static void test_score(void)
{
	score("crane", "crane", "GGGGG");
	score("crane", "pilot", ".....");
	score("abbey", "babes", "YYGG.");	/* two b's, one green */
	score("speed", "erase", "Y..YY");	/* e's: two in answer */
	score("speed", "abide", "...YY");
	score("abide", "speed", "..Y.Y");
	score("robot", "floor", "..YGY");
	score("aba", "aaa", "G.G");		/* extra a gets nothing */
	score("aab", "baa", "YGY");
	score("llama", "hello", "..YY.");	/* both l's present, no greens */
	score("hello", "llama", "YY...");	/* only two l's to hand out */
	score("hello", "lolly", ".YGG.");	/* greens use up both l's */
	score("cat", "act", "YYG");
	score("zzzzzzzzzzzz", "zzzzzzzzzzzz", "GGGGGGGGGGGG");
	score("abcdefghijkl", "lkjihgfedcba", "YYYYYYYYYYYY");
}

static void test_game(void)
{
	struct game g;

	CHECK(game_init(&g, "robot", 6) == 0);
	CHECK(game_init(&g, "ro", 6) < 0);
	CHECK(game_init(&g, "Robot", 6) < 0);
	CHECK(game_init(&g, "robot", 0) < 0);
	CHECK(game_init(&g, "robot", MAX_TRIES + 1) < 0);
	CHECK(game_init(&g, "abcdefghijklm", 6) < 0);
	CHECK(game_init(&g, "robot", 2) == 0);
	CHECK(game_submit(&g, "robo") < 0);		/* wrong length */
	CHECK(game_submit(&g, "ROBOT") < 0);
	CHECK(game_submit(&g, "floor") == 0);
	CHECK(g.key['o' - 'a'] == S_CORRECT);
	CHECK(g.key['f' - 'a'] == S_ABSENT);
	CHECK(g.key['r' - 'a'] == S_PRESENT);
	CHECK(g.key['z' - 'a'] == S_NONE);
	CHECK(g.state == G_PLAY && g.nguess == 1);
	CHECK(game_submit(&g, "boost") == 0);
	CHECK(g.state == G_LOST);
	CHECK(game_submit(&g, "robot") < 0);		/* game over */

	game_init(&g, "abbey", 6);
	game_submit(&g, "babes");
	CHECK(g.key['b' - 'a'] == S_CORRECT);		/* never downgraded */
	game_submit(&g, "abbey");
	CHECK(g.state == G_WON && g.nguess == 2);
}

static void test_len(void)
{
	int l;

	CHECK(!len_ok(-1) && !len_ok(0) && !len_ok(2));
	CHECK(len_ok(3) && len_ok(5) && len_ok(12));
	CHECK(!len_ok(13) && !len_ok(100));
	CHECK(game_tries(3) == 6 && game_tries(5) == 6);
	CHECK(game_tries(6) == 7 && game_tries(12) == 13);
	for (l = MIN_LEN; l <= MAX_LEN; l++)
		CHECK(game_tries(l) >= 6 && game_tries(l) <= MAX_TRIES);
}

static const char text[] =
    "# comment\r\n"
    "cat\ndog\r\nHORSE\nhorse\n  mouse  \n"
    "ab\nab1c\nhas space\nnaïve\n"
    "\n"
    "abcdefghijklm\n"		/* 13: too long */
    "abcdefghijkl\n"		/* 12 */
    "zebra";			/* no trailing newline */

static void test_dict(void)
{
	struct dict d;
	struct rng r;
	char buf[64], err[256];
	const char *w;
	int i;

	CHECK(dict_parse(&d, text, sizeof text - 1) == 6);
	CHECK(dict_count(&d, 3) == 2);
	CHECK(dict_count(&d, 5) == 3);			/* horse deduped, case folded */
	CHECK(dict_count(&d, 12) == 1);
	CHECK(dict_count(&d, 4) == 0 && dict_count(&d, 11) == 0);
	CHECK(dict_count(&d, 2) == 0 && dict_count(&d, 13) == 0 && dict_count(&d, -1) == 0);
	CHECK(dict_total(&d) == 6);
	CHECK(dict_has(&d, "horse", 5) && dict_has(&d, "zebra", 5));
	CHECK(!dict_has(&d, "hores", 5));
	CHECK(!dict_has(&d, "horse", 4));		/* length mismatch */
	CHECK(!dict_has(&d, "ab", 2));
	CHECK(!dict_has(&d, "", 0));
	dict_lengths(&d, buf, sizeof buf);
	CHECK(strcmp(buf, "3 5 12") == 0);

	/* unsupported length: nothing to pick */
	rng_seed(&r, 1);
	CHECK(dict_pick(&d, 4, &r) == NULL);
	for (i = 0; i < 200; i++) {
		w = dict_pick(&d, 5, &r);
		CHECK(w != NULL && dict_has(&d, w, 5));
	}
	dict_free(&d);

	CHECK(dict_parse(&d, "", 0) == 0);
	CHECK(dict_total(&d) == 0);
	dict_free(&d);

	CHECK(dict_load(&d, "/nonexistent/words.txt", err, sizeof err) < 0);
	CHECK(strstr(err, "/nonexistent/words.txt") != NULL);
	CHECK(dict_find("/nonexistent/words.txt", buf, sizeof buf, err, sizeof err) < 0);
	CHECK(strstr(err, "/nonexistent/words.txt") != NULL);
}

static void test_rng(void)
{
	struct rng a, b;
	long cnt[6] = { 0 };
	int i;

	rng_seed(&a, 42);
	rng_seed(&b, 42);
	for (i = 0; i < 100; i++)
		CHECK(rng_next(&a) == rng_next(&b));	/* deterministic */
	rng_seed(&a, 7);
	for (i = 0; i < 60000; i++) {
		uint64_t v = rng_below(&a, 6);

		CHECK(v < 6);
		cnt[v]++;
	}
	for (i = 0; i < 6; i++)
		CHECK(cnt[i] > 9500 && cnt[i] < 10500);	/* roughly uniform */
	CHECK(rng_below(&a, 0) == 0 && rng_below(&a, 1) == 0);
}

int main(void)
{
	test_score();
	test_game();
	test_len();
	test_dict();
	test_rng();
	printf("%d checks, %d failed\n", runs, fails);
	return fails != 0;
}
