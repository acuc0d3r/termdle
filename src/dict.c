#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "dict.h"

#ifndef WORDLE_DATADIR
#define WORDLE_DATADIR "/usr/local/share/wordle"
#endif
#define MAXFILE (32L * 1024 * 1024)

static int cmp(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

static int push(struct dict *d, int len, char *w)
{
	if (d->n[len] == d->cap[len]) {
		size_t nc = d->cap[len] ? d->cap[len] * 2 : 64;
		char **p = realloc(d->w[len], nc * sizeof *p);

		if (!p)
			return -1;
		d->w[len] = p;
		d->cap[len] = nc;
	}
	d->w[len][d->n[len]++] = w;
	return 0;
}

/* d->pool must hold a NUL-terminated copy of the text; it is edited in place */
static long parse_pool(struct dict *d)
{
	char *p, *nl, *s, *e;
	size_t len, i, j;
	long cnt = 0;
	int l, ok;

	for (p = d->pool; *p; p = nl) {
		if ((nl = strchr(p, '\n')) != NULL)
			*nl++ = '\0';
		else
			nl = p + strlen(p);
		for (s = p; *s && isspace((unsigned char)*s); s++)
			;
		len = strlen(s);
		for (e = s + len; e > s && isspace((unsigned char)e[-1]); e--)
			;
		*e = '\0';
		len = (size_t)(e - s);
		if (*s == '#' || len < MIN_LEN || len > MAX_LEN)
			continue;
		for (ok = 1, i = 0; i < len; i++) {
			if (!isalpha((unsigned char)s[i]) || (unsigned char)s[i] > 127) {
				ok = 0;
				break;
			}
			s[i] = (char)tolower((unsigned char)s[i]);
		}
		if (!ok)
			continue;
		if (push(d, (int)len, s) < 0)
			return -1;
	}
	for (l = MIN_LEN; l <= MAX_LEN; l++) {
		if (d->n[l] == 0)
			continue;
		qsort(d->w[l], d->n[l], sizeof *d->w[l], cmp);
		for (i = j = 1; i < d->n[l]; i++)
			if (strcmp(d->w[l][i], d->w[l][j - 1]) != 0)
				d->w[l][j++] = d->w[l][i];
		d->n[l] = j;
		cnt += (long)j;
	}
	return cnt;
}

long dict_parse(struct dict *d, const char *src, size_t n)
{
	long r;

	memset(d, 0, sizeof *d);
	if ((d->pool = malloc(n + 1)) == NULL)
		return -1;
	memcpy(d->pool, src, n);
	d->pool[n] = '\0';
	if ((r = parse_pool(d)) < 0)
		dict_free(d);
	return r;
}

int dict_load(struct dict *d, const char *path, char *err, size_t esz)
{
	FILE *f;
	long sz;

	memset(d, 0, sizeof *d);
	if ((f = fopen(path, "rb")) == NULL) {
		snprintf(err, esz, "%s: %s", path, strerror(errno));
		return -1;
	}
	if (fseek(f, 0, SEEK_END) || (sz = ftell(f)) < 0 || fseek(f, 0, SEEK_SET)) {
		snprintf(err, esz, "%s: not a regular file", path);
		fclose(f);
		return -1;
	}
	if (sz > MAXFILE) {
		snprintf(err, esz, "%s: file too large", path);
		fclose(f);
		return -1;
	}
	if ((d->pool = malloc((size_t)sz + 1)) == NULL ||
	    fread(d->pool, 1, (size_t)sz, f) != (size_t)sz) {
		snprintf(err, esz, "%s: read error", path);
		fclose(f);
		dict_free(d);
		return -1;
	}
	fclose(f);
	d->pool[sz] = '\0';
	if (parse_pool(d) < 0) {
		snprintf(err, esz, "out of memory");
		dict_free(d);
		return -1;
	}
	return 0;
}

void dict_free(struct dict *d)
{
	int l;

	for (l = 0; l <= MAX_LEN; l++)
		free(d->w[l]);
	free(d->pool);
	memset(d, 0, sizeof *d);
}

size_t dict_count(const struct dict *d, int len)
{
	return len_ok(len) ? d->n[len] : 0;
}

size_t dict_total(const struct dict *d)
{
	size_t t = 0;
	int l;

	for (l = MIN_LEN; l <= MAX_LEN; l++)
		t += d->n[l];
	return t;
}

int dict_has(const struct dict *d, const char *w, int len)
{
	if (!len_ok(len) || strlen(w) != (size_t)len || d->n[len] == 0)
		return 0;
	return bsearch(&w, d->w[len], d->n[len], sizeof *d->w[len], cmp) != NULL;
}

const char *dict_pick(const struct dict *d, int len, struct rng *r)
{
	if (dict_count(d, len) == 0)
		return NULL;
	return d->w[len][rng_below(r, d->n[len])];
}

void dict_lengths(const struct dict *d, char *buf, size_t sz)
{
	size_t off = 0;
	int l, n;

	if (sz == 0)
		return;
	buf[0] = '\0';
	for (l = MIN_LEN; l <= MAX_LEN && off < sz; l++) {
		if (d->n[l] == 0)
			continue;
		n = snprintf(buf + off, sz - off, off ? " %d" : "%d", l);
		if (n < 0)
			break;
		off += (size_t)n;
	}
}

static void note(char *tried, size_t tsz, const char *p)
{
	size_t n = strlen(tried);

	if (n < tsz)
		snprintf(tried + n, tsz - n, "  %s\n", p);
}

static int probe(const char *p, char *out, size_t sz, char *tried, size_t tsz)
{
	if (access(p, R_OK) == 0 && strlen(p) < sz) {
		strcpy(out, p);
		return 1;
	}
	note(tried, tsz, p);
	return 0;
}

/* explicit -d / $WORDLE_DICT is never second-guessed; otherwise search */
int dict_find(const char *opt, char *out, size_t sz, char *tried, size_t tsz)
{
	char exe[PATH_MAX], cand[PATH_MAX + 64];
	const char *env;
	char *sl;
	ssize_t n;

	if (tsz)
		tried[0] = '\0';
	if (opt)
		return probe(opt, out, sz, tried, tsz) ? 0 : -1;
	if ((env = getenv("WORDLE_DICT")) != NULL && *env)
		return probe(env, out, sz, tried, tsz) ? 0 : -1;
	n = readlink("/proc/self/exe", exe, sizeof exe - 1);
	if (n > 0) {
		exe[n] = '\0';
		if ((sl = strrchr(exe, '/')) != NULL)
			*sl = '\0';
		snprintf(cand, sizeof cand, "%s/../share/wordle/words.txt", exe);
		if (probe(cand, out, sz, tried, tsz))
			return 0;
	}
	if (probe(WORDLE_DATADIR "/words.txt", out, sz, tried, tsz))
		return 0;
	if (n > 0) {
		snprintf(cand, sizeof cand, "%s/data/words.txt", exe);
		if (probe(cand, out, sz, tried, tsz))
			return 0;
	}
	return probe("data/words.txt", out, sz, tried, tsz) ? 0 : -1;
}
