#ifndef CLIENT_H_
#define CLIENT_H_

#include <pthread.h>

#include "net/connection.h"
#include "controller.h"
#include "api/environment.h"
#include "handler.h"
#include "list.h"
#include "net/message.h"
#include "types.h"

struct client {
	struct list_head l_clients;
	int id;
	struct connection *con;
	struct {
		int codec_version[2];
		int use;
	} celt;
	pthread_mutex_t m_celt;
	char *username;
	char *password;
	uint32_t session;
	pthread_t engine;
	bool running;
	bool exit;
	bool restart;
	struct list_head profiles;
	struct environment *env;
	char *plugindir;
	char *packagedir;
	struct list_head plugins;
	char *f_privilege;
	struct list_head privileges;
	pthread_mutex_t m_privilege;
};

struct client * client_new(char *, char *, char *, char *, char *, char *, char *, char *);

bool client_free(struct client *);

int client_get_celt_codec_version(struct client *, int *);

void client_set_celt_codec_version(struct client *, int, int, int);

struct list_head * client_list();

#endif /* CLIENT_H_ */
