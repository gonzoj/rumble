#ifndef HANDLER_H_
#define HANDLER_H_

#include "list.h"
#include "net/message.h"
#include "types.h"

/* forward declaration to avoid circular dependency with client.h */
struct client;

struct handler {
	void (*process)(struct client *, void *);
	bool registered;
};

//typedef struct handler profile[NUM_MESSAGES];
struct profile {
	struct handler handler[NUM_MESSAGES];
	struct list_head l_profiles;
};

#define handler_profile_register(p, t, h) p->handler[t].process = (void (*)(struct client *, void *)) h; p->handler[t].registered = TRUE
#define handler_profile_unregister(p, t) p->handler[t].process = NULL; p->handler[t].registered = FALSE

#define handler_profile_new(p) memset(p->handler, 0, sizeof(p->handler));

#define handler_profile_free(p) free(p)

#define for_each_profile(p, l) list_for_each_entry(p, &l, l_profiles)

void handler_profile_bot(struct list_head *);

#endif /* HANDLER_H_ */
