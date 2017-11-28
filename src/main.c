#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "celtcodec.h"
#include "client.h"
#include "config.h"
#include "net/connection.h"
#include "console.h"
#include "api/sound.h"
#include "types.h"
#include "version.h"

static bool sigint = FALSE;

void sigint_handler(int signal) {
	sigint = TRUE;
}

static void run() {
	struct client *bot = client_new(settings.host, settings.port, settings.cert, settings.username, settings.password, "plugins/", "packages/", "privilege.lua");

	while (!sigint) {
		if (!bot->running) {
			bool retry = client_free(bot);

			if (retry) {
				bot = client_new(settings.host, settings.port, settings.cert, settings.username, settings.password, "plugins/", "packages/", "privilege.lua");
			} else {
				bot = NULL;
				break;
			}
		}

		sleep(1);
	}

	if (bot) client_free(bot);
}

int main(int argc, char **argv) {
	int status = EXIT_FAILURE;

	if (config_parse_arguments(argc, argv)) {
		fprintf(stderr, "error while parsing arguments\n");
		fprintf(stderr, "run rumble --help for a usage description\n");

		goto exit;
	}

	version_string s;
	printf("rumble version %s-%s\n\n", version_to_string(_version, s), _release);

	char *home = getenv("HOME");
	if (!home) {
		fprintf(stderr, "failed to retrieve $HOME\n");

		goto exit;
	}

	/*if (chdir(home) || chdir(".rumble")) {
		fprintf(stderr, "failed to change to directory %s/%s (%s)\n", home, ".rumble", strerror(errno));

		goto exit;
	}*/
	if (chdir("..")) {
		fprintf(stderr, "failed to change to directory %s (%s)\n", "..", strerror(errno));

		goto exit;
	}

	console_init();

	openssl_init();

	celtcodec_load_all();

	sound_av_init();

	signal(SIGINT, sigint_handler);
	signal(SIGPIPE, SIG_IGN);

	run();

	celtcodec_free_all();

	openssl_cleanup();

	console_free();

	status = EXIT_SUCCESS;

	exit:
	printf("\nexiting\n");

	exit(status);
}
