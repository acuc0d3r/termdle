#ifndef DICT_H
#define DICT_H

#include <stddef.h>
#include "game.h"
#include "rng.h"

struct dict {
	char *pool;
	char **w[MAX_LEN + 1];		/* sorted, unique, per length */
	size_t n[MAX_LEN + 1], cap[MAX_LEN + 1];
};

int dict_find(const char *opt, char *out, size_t sz, char *tried, size_t tsz);
int dict_load(struct dict *d, const char *path, char *err, size_t esz);
long dict_parse(struct dict *d, const char *src, size_t n);
void dict_free(struct dict *d);
size_t dict_count(const struct dict *d, int len);
size_t dict_total(const struct dict *d);
int dict_has(const struct dict *d, const char *w, int len);
const char *dict_pick(const struct dict *d, int len, struct rng *r);
void dict_lengths(const struct dict *d, char *buf, size_t sz);

#endif
