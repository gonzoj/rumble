#include <stdlib.h>

#include "../types.h"

int varint_decode(unsigned char *varint, uint64_t *v) {
	int len = 0;

	uint8_t p = varint[0];

	if ((p & 0x80) == 0x00) {
		len = 1;
		*v = p & 0x7F;
	} else if ((p & 0xC0) == 0x80) {
		len = 2;
		*v = (p & 0x3F) << 8 | varint[1];
	} else if ((p & 0xE0) == 0xC0) {
		len = 3;
		*v = (p & 0x1F) << 16 | varint[1] << 8 | varint[2];
	} else if ((p & 0xF0) == 0xE0) {
		len = 4;
		*v = (p & 0x0F) << 24 | varint[1] << 16 | varint[2] << 8 | varint[3];
	} else if ((p & 0xF0) == 0xF0) {
		switch (p & 0x0C) {
			case 0x00:
				len = 5;
				*v = varint[1] << 24 | varint[2] << 16 | varint[3] << 8 | varint[4];
				break;
			case 0x04:
				len = 9;
				*v = ((uint64_t) varint[1]) << 56 | ((uint64_t) varint[2]) << 48 | ((uint64_t) varint[3]) << 40 | ((uint64_t) varint[4]) << 32 | varint[5] << 24 | varint[6] << 16 | varint[7] << 8 | varint[8];
				break;
			case 0x08:
				len = 1 + varint_decode(&varint[1], v);
				*v = ~*v;
				break;
			case 0x0C:
				len = 1;
				*v = p & 0x03;
				*v = ~*v;
				break;
			default:
				*v = 0;
				break;
		}
	}

	return len;
}

int varint_encode(uint64_t v, unsigned char **varint) {
	int len = 0;
	int i = 0;
	*varint = NULL;

	if ((v & 0x8000000000000000LL) && (~v < 0x100000000LL)) {
		v = ~v;

		if (v <= 0x03) {
			len = 1;
			*varint = malloc(len);
			(*varint)[i++] = 0xFC | v;

			return len;
		} else {
			len += 1;
			*varint = malloc(len);
			(*varint)[i++] = 0xF8;
		}
	}

	if (v < 0x80) {
		len += 1;
		*varint = realloc(*varint, len);
		(*varint)[i++] = v;
	} else if (v < 0x4000) {
		len += 2;
		*varint = realloc(*varint, len);
		(*varint)[i++] = v >> 8 | 0x80;
		(*varint)[i++] = v & 0xFF;
	} else if (v < 0x200000) {
		len += 3;
		*varint = realloc(*varint, len);
		(*varint)[i++] = v >> 16 | 0xC0;
		(*varint)[i++] = v >> 8 & 0xFF;
		(*varint)[i++] = v & 0xFF;
	} else if (v < 0x10000000) {
		len += 4;
		*varint = realloc(*varint, len);
		(*varint)[i++] = v >> 24 | 0xE0;
		(*varint)[i++] = v >> 16 & 0xFF;
		(*varint)[i++] = v >> 8 & 0xFF;
		(*varint)[i++] = v & 0xFF;
	} else if (v < 0x100000000LL) {
		len += 5;
		*varint = realloc(*varint, len);
		(*varint)[i++] = 0xF0;
		(*varint)[i++] = v >> 24 & 0xFF;
		(*varint)[i++] = v >> 16 & 0xFF;
		(*varint)[i++] = v >> 8 & 0xFF;
		(*varint)[i++] = v & 0xFF;
	} else {
		len += 9;
		*varint = realloc(*varint, len);
		(*varint)[i++] = 0xF4;
		(*varint)[i++] = v >> 56 & 0xFF;
		(*varint)[i++] = v >> 48 & 0xFF;
		(*varint)[i++] = v >> 40 & 0xFF;
		(*varint)[i++] = v >> 32 & 0xFF;
		(*varint)[i++] = v >> 24 & 0xFF;
		(*varint)[i++] = v >> 16 & 0xFF;
		(*varint)[i++] = v >> 8 & 0xFF;
		(*varint)[i++] = v & 0xFF;
	}

	return len;
}
