#ifndef CONFIG_H_
#define CONFIG_H_

#include "types.h"

typedef char string_setting[512];

struct config {
	string_setting host;
	string_setting port;
	string_setting cert;
	string_setting username;
	string_setting password;
	bool log;
	bool debug;
	int bitrate;
	int frames;
	float volume;
};

extern struct config settings;

int config_parse_arguments(int, char **);

#endif /* CONFIG_H_ */
