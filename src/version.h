#ifndef VERSION_H_
#define VERSION_H_

#include <stdio.h>

#define __version(mj, mn, pt) (uint32_t) ((mj << 16) | (mn << 8) | (pt))

#define _version __version(0, 1, 1)
#define _release "alpha"

typedef char version_string[14];

static inline char * version_to_string(uint32_t v, version_string s) {
	snprintf(s, sizeof(version_string), "%u.%u.%u", (v >> 16) & 0xFFFF, (v >> 8) & 0xFF, v & 0xFF);

	return s;
}

#endif /* VERSION_H_ */
