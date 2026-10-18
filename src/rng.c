#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include "rng.h"

/* splitmix64 */
void rng_seed(struct rng *r, uint64_t seed)
{
	r->s = seed;
}

uint64_t rng_next(struct rng *r)
{
	uint64_t z = (r->s += 0x9e3779b97f4a7c15ULL);

	z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
	z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
	return z ^ (z >> 31);
}

uint64_t rng_below(struct rng *r, uint64_t n)
{
	uint64_t x, min;

	if (n < 2)
		return 0;
	min = -n % n;		/* 2^64 mod n: drop the short final bucket */
	do
		x = rng_next(r);
	while (x < min);
	return x % n;
}

uint64_t rng_os_seed(void)
{
	uint64_t s = 0;
	struct timespec ts;
	FILE *f = fopen("/dev/urandom", "rb");

	if (f) {
		size_t n = fread(&s, sizeof s, 1, f);

		fclose(f);
		if (n == 1)
			return s;
	}
	clock_gettime(CLOCK_REALTIME, &ts);
	return (uint64_t)ts.tv_nsec ^ ((uint64_t)ts.tv_sec << 20) ^
	    ((uint64_t)getpid() << 40);
}
