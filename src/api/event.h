#ifndef EVENT_H_
#define EVENT_H_

#include <pthread.h>

#include "../plugin.h"
#include "../types.h"

struct tick {
	pthread_t tid;
	bool shutdown;
	int freq;
};

void user_joined_server_event(struct plugin *, void *, int);

void command_message_event(struct plugin *, void *, int);

void text_message_event(struct plugin *, void *, int);

void playback_event(struct plugin *, void *, int);

void tick_event(struct plugin *, void *, int);

void * tick(void *);

void user_stats_event(struct plugin *, void *, int);

#endif /* EVENT_H_ */
