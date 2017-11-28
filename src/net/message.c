#include <arpa/inet.h>
#include <unistd.h>

#include "connection.h"
#include "message.h"
#include "audio.h"
#include "../types.h"

#define MESSAGE_HEADER_SIZE (sizeof(uint16_t) + sizeof(uint32_t))

#define set_message_type(m, t) *(uint16_t *)(m) = htons(t)
#define set_message_length(m, l) *(uint32_t *)&(((uint8_t *)(m))[sizeof(uint16_t)]) = htonl(l)

#define get_message_type(m) ntohs(*(uint16_t *)(m))
#define get_message_length(m) ntohl(*(uint32_t *)&(((uint8_t *) (m))[sizeof(uint16_t)]))

const char *s_message[] = {
	"Version",
	"UDPTunnel",
	"Authenticate",
	"Ping",
	"Reject",
	"ServerSync",
	"ChannelRemove",
	"ChannelState",
	"UserRemove",
	"UserState",
	"BanList",
	"TextMessage",
	"PermissionDenied",
	"ACL",
	"QueryUsers",
	"CryptSetup",
	"ContextActionAdd",
	"ContextAction",
	"UserList",
	"VoiceTarget",
	"PermissionQuery",
	"CodecVersion",
	"UserStats",
	"RequestBlob",
	"ServerConfig",
	"SuggestConfig"
};

typedef size_t (*_get_packed_size)(void *);
typedef size_t (*_pack)(void *, uint8_t *);
typedef void * (*_unpack)(void *, size_t len, const uint8_t *);
typedef void (*_free_unpacked)(void *, void *);

static _get_packed_size message_get_packed_size[] = {
	(_get_packed_size) mumble_proto__version__get_packed_size,
	(_get_packed_size) mumble_proto__udptunnel__get_packed_size,
	(_get_packed_size) mumble_proto__authenticate__get_packed_size,
	(_get_packed_size) mumble_proto__ping__get_packed_size,
	(_get_packed_size) mumble_proto__reject__get_packed_size,
	(_get_packed_size) mumble_proto__server_sync__get_packed_size,
	(_get_packed_size) mumble_proto__channel_remove__get_packed_size,
	(_get_packed_size) mumble_proto__channel_state__get_packed_size,
	(_get_packed_size) mumble_proto__user_remove__get_packed_size,
	(_get_packed_size) mumble_proto__user_state__get_packed_size,
	(_get_packed_size) mumble_proto__ban_list__get_packed_size,
	(_get_packed_size) mumble_proto__text_message__get_packed_size,
	(_get_packed_size) mumble_proto__permission_denied__get_packed_size,
	(_get_packed_size) mumble_proto__acl__get_packed_size,
	(_get_packed_size) mumble_proto__query_users__get_packed_size,
	(_get_packed_size) mumble_proto__crypt_setup__get_packed_size,
	(_get_packed_size) mumble_proto__context_action_add__get_packed_size,
	(_get_packed_size) mumble_proto__context_action__get_packed_size,
	(_get_packed_size) mumble_proto__user_list__get_packed_size,
	(_get_packed_size) mumble_proto__voice_target__get_packed_size,
	(_get_packed_size) mumble_proto__permission_query__get_packed_size,
	(_get_packed_size) mumble_proto__codec_version__get_packed_size,
	(_get_packed_size) mumble_proto__user_stats__get_packed_size,
	(_get_packed_size) mumble_proto__request_blob__get_packed_size,
	(_get_packed_size) mumble_proto__server_config__get_packed_size
};

static _pack message_pack[] = {
	(_pack) mumble_proto__version__pack,
	(_pack) mumble_proto__udptunnel__pack,
	(_pack) mumble_proto__authenticate__pack,
	(_pack) mumble_proto__ping__pack,
	(_pack) mumble_proto__reject__pack,
	(_pack) mumble_proto__server_sync__pack,
	(_pack) mumble_proto__channel_remove__pack,
	(_pack) mumble_proto__channel_state__pack,
	(_pack) mumble_proto__user_remove__pack,
	(_pack) mumble_proto__user_state__pack,
	(_pack) mumble_proto__ban_list__pack,
	(_pack) mumble_proto__text_message__pack,
	(_pack) mumble_proto__permission_denied__pack,
	(_pack) mumble_proto__acl__pack,
	(_pack) mumble_proto__query_users__pack,
	(_pack) mumble_proto__crypt_setup__pack,
	(_pack) mumble_proto__context_action_add__pack,
	(_pack) mumble_proto__context_action__pack,
	(_pack) mumble_proto__user_list__pack,
	(_pack) mumble_proto__voice_target__pack,
	(_pack) mumble_proto__permission_query__pack,
	(_pack) mumble_proto__codec_version__pack,
	(_pack) mumble_proto__user_stats__pack,
	(_pack) mumble_proto__request_blob__pack,
	(_pack) mumble_proto__server_config__pack
};

static _unpack message_unpack[] = {
	(_unpack) mumble_proto__version__unpack,
	(_unpack) mumble_proto__udptunnel__unpack,
	(_unpack) mumble_proto__authenticate__unpack,
	(_unpack) mumble_proto__ping__unpack,
	(_unpack) mumble_proto__reject__unpack,
	(_unpack) mumble_proto__server_sync__unpack,
	(_unpack) mumble_proto__channel_remove__unpack,
	(_unpack) mumble_proto__channel_state__unpack,
	(_unpack) mumble_proto__user_remove__unpack,
	(_unpack) mumble_proto__user_state__unpack,
	(_unpack) mumble_proto__ban_list__unpack,
	(_unpack) mumble_proto__text_message__unpack,
	(_unpack) mumble_proto__permission_denied__unpack,
	(_unpack) mumble_proto__acl__unpack,
	(_unpack) mumble_proto__query_users__unpack,
	(_unpack) mumble_proto__crypt_setup__unpack,
	(_unpack) mumble_proto__context_action_add__unpack,
	(_unpack) mumble_proto__context_action__unpack,
	(_unpack) mumble_proto__user_list__unpack,
	(_unpack) mumble_proto__voice_target__unpack,
	(_unpack) mumble_proto__permission_query__unpack,
	(_unpack) mumble_proto__codec_version__unpack,
	(_unpack) mumble_proto__user_stats__unpack,
	(_unpack) mumble_proto__request_blob__unpack,
	(_unpack) mumble_proto__server_config__unpack
};

static _free_unpacked message_free_unpacked[] = {
	(_free_unpacked) mumble_proto__version__free_unpacked,
	(_free_unpacked) mumble_proto__udptunnel__free_unpacked,
	(_free_unpacked) mumble_proto__authenticate__free_unpacked,
	(_free_unpacked) mumble_proto__ping__free_unpacked,
	(_free_unpacked) mumble_proto__reject__free_unpacked,
	(_free_unpacked) mumble_proto__server_sync__free_unpacked,
	(_free_unpacked) mumble_proto__channel_remove__free_unpacked,
	(_free_unpacked) mumble_proto__channel_state__free_unpacked,
	(_free_unpacked) mumble_proto__user_remove__free_unpacked,
	(_free_unpacked) mumble_proto__user_state__free_unpacked,
	(_free_unpacked) mumble_proto__ban_list__free_unpacked,
	(_free_unpacked) mumble_proto__text_message__free_unpacked,
	(_free_unpacked) mumble_proto__permission_denied__free_unpacked,
	(_free_unpacked) mumble_proto__acl__free_unpacked,
	(_free_unpacked) mumble_proto__query_users__free_unpacked,
	(_free_unpacked) mumble_proto__crypt_setup__free_unpacked,
	(_free_unpacked) mumble_proto__context_action_add__free_unpacked,
	(_free_unpacked) mumble_proto__context_action__free_unpacked,
	(_free_unpacked) mumble_proto__user_list__free_unpacked,
	(_free_unpacked) mumble_proto__voice_target__free_unpacked,
	(_free_unpacked) mumble_proto__permission_query__free_unpacked,
	(_free_unpacked) mumble_proto__codec_version__free_unpacked,
	(_free_unpacked) mumble_proto__user_stats__free_unpacked,
	(_free_unpacked) mumble_proto__request_blob__free_unpacked,
	(_free_unpacked) mumble_proto__server_config__free_unpacked
};

int message_send(struct connection *con, uint16_t type, void *message) {
	if (!message_type_is_valid(type)) return -1;

	uint32_t len;
	unsigned char *buf;

	if (type != m_UDPTUNNEL) {
		len = message_get_packed_size[type](message);

		buf = malloc(MESSAGE_HEADER_SIZE + len);

		message_pack[type](message, buf + MESSAGE_HEADER_SIZE);
	} else {
		buf = malloc(MESSAGE_HEADER_SIZE);

		len = audio_serialize((struct packet *) message, &buf, MESSAGE_HEADER_SIZE);
	}

	set_message_type(buf, type);
	set_message_length(buf, len);

	int r = control_send(con, buf, MESSAGE_HEADER_SIZE + len);

	free(buf);

	return r;
}

int message_recv(struct connection *con, uint16_t *type, void **message) {
	unsigned char header[MESSAGE_HEADER_SIZE];

	int r;

	if ((r = control_recv(con, header, MESSAGE_HEADER_SIZE)) <= 0) {
		return r;
	}

	uint16_t t;
	uint32_t len;

	t = get_message_type(header);
	len = get_message_length(header);

	*type = t;

	if (!message_type_is_valid(t)) return -1;

	unsigned char *buf = malloc(len);

	/*if ((r = control_recv(con, buf, len)) < 0) {
		free(buf);

		return -1;
	}*/

	do {
		r = control_recv(con, buf, len);
	} while (!r);

	if (r > 0) {
		if (t != m_UDPTUNNEL) {
			*message = message_unpack[t](NULL, len, buf);
		} else {
			audio_deserialize((struct packet **) message, buf, len);
		}
	}

	free(buf);

	return r;
}

void message_free(uint16_t type, void *message) {
	if (type != m_UDPTUNNEL) {
		message_free_unpacked[type](message, NULL);
	} else {
		audio_free((struct packet *) message);
	}
}
