#ifndef RNG_H
#define RNG_H

#include <stdint.h>

struct rng { uint64_t s; };

void rng_seed(struct rng *r, uint64_t seed);
uint64_t rng_next(struct rng *r);
uint64_t rng_below(struct rng *r, uint64_t n);	/* uniform in [0, n) */
uint64_t rng_os_seed(void);

#endif
