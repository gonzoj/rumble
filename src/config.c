#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "types.h"

struct config settings = {
	.host = "localhost",
	.port = "64738",
	.cert = "/path/to/cert.pem",
	.username = "rumble",
	.password = "password",
	.log = FALSE,
	.debug = FALSE,
	.bitrate = 40000, 
	.frames = 2,
	.volume = 0.10
};

static void usage() {
	printf("USAGE:\n");
	printf("\n");
	printf("rumble [OPTIONS]\n");
	printf("\n");
	printf("OPTIONS:\n");
	printf("	--version\n");
	printf("		print version and exit\n");
	printf("\n");
	printf("	--help\n");
	printf("		print usage description and exit\n");
	printf("\n");
	printf("	--host HOSTNAME, -h HOSTNAME\n");
	printf("		connect to HOSTNAME\n");
	printf("\n");
	printf("	--port SERVICE, -s SERVICE\n");
	printf("		service bound to port SERVICE\n");
	printf("\n");
	printf("	--cert FILE, -c FILE\n");
	printf("		use certificate/key from PEM file FILE\n");
	printf("\n");
	printf("	--user NAME, -u NAME\n");
	printf("		connect as user NAME\n");
	printf("\n");
	printf("	--pass PASS, -p PASS\n");
	printf("		provide password PASS when authenticating\n");
	printf("\n");
	printf("	--log, -l\n");
	printf("		enable log file\n");
	printf("\n");
	printf("	--debug, -d\n");
	printf("		enable debug output\n");
	printf("\n");
	printf("	--bitrate RATE, -b RATE\n");
	printf("		use bitrate RATE for audio compression\n");
	printf("\n");
	printf("	--frames FRAMES, -f FRAMES\n");
	printf("		send FRAMES audio frames per packet\n");
	printf("\n");
	printf("	--volume VOLUME, -f VOLUME\n");
	printf("		set default volume of voice transmission to VOLUME\n");
	printf("\n");
}

int config_parse_arguments(int argc, char **argv) {
	struct option long_options[] = {
		{ "version", no_argument, NULL, 0 },
		{ "help", no_argument, NULL, 1 },
		{ "host", required_argument, NULL, 'h' },
		{ "port", required_argument, NULL, 's' },
		{ "cert", required_argument, NULL, 'c' },
		{ "user", required_argument, NULL, 'u' },
		{ "pass", required_argument, NULL, 'p' },
		{ "log", no_argument, NULL, 'l' },
		{ "debug", no_argument, NULL, 'd' },
		{ "bitrate", required_argument, NULL, 'b' },
		{ "frames", required_argument, NULL, 'f' },
		{ "volume", required_argument, NULL, 'v' },
		{ 0 }
	};

	while (optind < argc) {
		int index = -1;
		int result = getopt_long(argc, argv, "h:s:c:u:p:ldb:f:v:", long_options, &index);
		if (result == -1) return -1;

		switch (result) {
			case 0: exit(EXIT_SUCCESS);
			case 1: usage(); exit(EXIT_SUCCESS);
			case 'h': strncpy(settings.host, optarg, sizeof(settings.host)); break;
			case 's': strncpy(settings.port, optarg, sizeof(settings.port)); break;
			case 'c': strncpy(settings.cert, optarg, sizeof(settings.cert)); break;
			case 'u': strncpy(settings.username, optarg, sizeof(settings.username)); break;
			case 'p': strncpy(settings.password, optarg, sizeof(settings.password)); break;
			case 'l': settings.log = TRUE; break;
			case 'd': settings.debug = TRUE; break;
			case 'b': sscanf(optarg, "%i", &settings.bitrate); break;
			case 'f': sscanf(optarg, "%i", &settings.frames); break;
			case 'v': sscanf(optarg, "%f", &settings.volume); break;

			case '?':
			case ':':
			default: return -1;
		}
	}

	return 0;
}
