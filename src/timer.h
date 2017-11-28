#ifndef TIMER_H_
#define TIMER_H_

#include <sys/time.h>

#include "types.h"

#define timestamp(tv) ((uint64_t) tv.tv_sec * 1000000ULL + (uint64_t) tv.tv_usec)

#define TIMER_INIT (struct timer) { .start = timer_now() }

struct timer {
	uint64_t start;
};

static inline uint64_t timer_now() {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return timestamp(tv);
}

static inline void timer_new(struct timer *t) {
	t->start = timer_now();
}

static inline uint64_t timer_elapsed(struct timer *t) {
	return timer_now() - t->start;
}

static inline bool timer_is_elapsed(struct timer *t, uint64_t p) {
	if (timer_elapsed(t) > p) {
		t->start += p;

		return TRUE;
	}
	
	return FALSE;
}

static inline uint64_t timer_restart(struct timer *t) {
	uint64_t n = timer_now();
	uint64_t e = n - t->start;
	t->start = n;
	return e;
}

#endif /* TIMER_H_ */
