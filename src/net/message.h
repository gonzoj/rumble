#ifndef MESSAGE_H_
#define MESSAGE_H_

#include "connection.h"
#include "../types.h"

#include "protobuf/Mumble.pb-c.h"

enum {
	m_VERSION,
	m_UDPTUNNEL,
	m_AUTHENTICATE,
	m_PING,
	m_REJECT,
	m_SERVER_SYNC,
	m_CHANNEL_REMOVE,
	m_CHANNEL_STATE,
	m_USER_REMOVE,
	m_USER_STATE,
	m_BAN_LIST,
	m_TEXT_MESSAGE,
	m_PERMISSION_DENIED,
	m_ACL,
	m_QUERY_USERS,
	m_CRYPT_SETUP,
	m_CONTEXT_ACTION_ADD,
	m_CONTEXT_ACTION,
	m_USER_LIST,
	m_VOICE_TARGET,
	m_PERMISSION_QUERY,
	m_CODEC_VERSION,
	m_USER_STATS,
	m_REQUEST_BLOB,
	m_SERVER_CONFIG,
	m_SUGGEST_CONFIG,
	__OUT_OF_BOUNDS
};

#define NUM_MESSAGES __OUT_OF_BOUNDS

extern const char *s_message[];

#define message_type_is_valid(t) ((t >= 0) && (t < __OUT_OF_BOUNDS))

#define message_new(m) MUMBLE_PROTO__##m##__INIT

#define message_set_optional(m, f, v) m.f = v; m.has_##f = TRUE

#define message_add_repeated(m, f, v) m.n_##f++; m.f = realloc(m.f, m.n_##f * sizeof(m.f)); m.f[m.n_##f - 1] = v
#define message_free_repeated(m, f) free(m.f)
#define message_for_each_repeated(m, f, i) for (i = 0; i < m->n_##f; i++)

int message_send(struct connection *, uint16_t, void *);

int message_recv(struct connection *, uint16_t *, void **);

void message_free(uint16_t, void *);

#endif /* MESSAGE_H_ */
