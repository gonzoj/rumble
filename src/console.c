#define _GNU_SOURCE

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>

#include "config.h"
#include "console.h"

static pthread_mutex_t console_m;

static FILE *f_message;
static FILE *f_warning;
static FILE *f_error;
static FILE *f_debug;

void console_init() {
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&console_m, &attr);
	pthread_mutexattr_destroy(&attr);

	f_message = stdout;
	f_warning = stderr;
	f_error = stderr;
	f_debug = stdout;
}

void console_free() {
	pthread_mutex_destroy(&console_m);
}

void console_lock() {
	pthread_mutex_lock(&console_m);
}

void console_unlock() {
	pthread_mutex_unlock(&console_m);
}

FILE * console_get_message_file() {
	return f_message;
}

FILE * console_get_warning_file() {
	return f_warning;
}

FILE * console_get_error_file() {
	return f_error;
}

FILE * console_get_debug_file() {
	return f_debug;
}

void console_print_v(int type, const char *format, va_list args) {
	if (type == CONSOLE_DEBUG && !settings.debug) return;

	console_lock();

	FILE *f;
	switch (type) {
		default:
		case CONSOLE_MESSAGE: f = f_message; break;
		case CONSOLE_WARNING: f = f_warning; break; 
		case CONSOLE_ERROR: f = f_error; break;
		case CONSOLE_DEBUG: f = f_debug; break;
	}

	va_list _args;
	va_copy(_args, args);

	vfprintf(f, format, args);

	if (settings.log) {
		FILE *l = fopen("rumble.log", "a");
		if (l) {
			vfprintf(f, format, _args);

			fclose(l);
		}
	}

	va_end(_args);

	console_unlock();
}

void console_print(int type, const char *format, ...) {
	/*if (type == CONSOLE_DEBUG && !settings.debug) return;

	console_lock();

	FILE *f;
	switch (type) {
		default:
		case CONSOLE_MESSAGE: f = f_message; break;
		case CONSOLE_WARNING: f = f_warning; break; 
		case CONSOLE_ERROR: f = f_error; break;
		case CONSOLE_DEBUG: f = f_debug; break;
	}

	va_list args;
	va_start(args, format);

	vfprintf(f, format, args);

	va_end(args);

	if (settings.log) {
		FILE *l = fopen("rumble.log", "a");
		if (l) {
			va_start(args, format);

			vfprintf(f, format, args);

			va_end(args);

			fclose(l);
		}
	}

	console_unlock();*/

	va_list args;
	va_start(args, format);

	console_print_v(type, format, args);

	va_end(args);
}

void console_print_message_v(int type, const char *class, const char *subclass, const char *format, va_list args) {
	if (type == CONSOLE_DEBUG && !settings.debug) return;

	console_lock();

	FILE *f;
	char *t;
	switch (type) {
		default:
		case CONSOLE_MESSAGE: f = f_message; t = ""; break;
		case CONSOLE_WARNING: f = f_warning; t = "WARNING *** "; break;
		case CONSOLE_ERROR: f = f_error; t = "ERROR *** "; break;
		case CONSOLE_DEBUG: f = f_debug; t = "DEBUG *** "; break;
	}

	if (subclass) fprintf(f, "%s[%s::%s::%04lu] ", t, class, subclass, (unsigned long) pthread_self() % 10000);
	else fprintf(f, "%s[%s::%04lu] ", t, class, (unsigned long) pthread_self() % 10000);

	va_list _args;
	va_copy(_args, args);

	vfprintf(f, format, args);

	if (settings.log) {
		FILE *l = fopen("rumble.log", "a");
		if (l) {
			time_t ti = time(NULL);
			struct tm tm;
			localtime_r(&ti, &tm);

			char ts[512];
			strftime(ts, sizeof(ts), "%c", &tm);

			fprintf(l, "(%s) ", ts);

			if (subclass) fprintf(l, "%s[%s::%s::%04lu] ", t, class, subclass, (unsigned long) pthread_self() % 10000);
			else fprintf(l, "%s[%s::%04lu] ", t, class, (unsigned long) pthread_self() % 10000);

			vfprintf(l, format, _args);

			fclose(l);
		}
	}

	va_end(_args);

	console_unlock();
}

void console_print_message(int type, const char *class, const char *subclass, const char *format, ...) {
	/*if (type == CONSOLE_DEBUG && !settings.debug) return;

	console_lock();

	FILE *f;
	char *t;
	switch (type) {
		default:
		case CONSOLE_MESSAGE: f = f_message; t = ""; break;
		case CONSOLE_WARNING: f = f_warning; t = "WARNING *** "; break;
		case CONSOLE_ERROR: f = f_error; t = "ERROR *** "; break;
		case CONSOLE_DEBUG: f = f_debug; t = "DEBUG *** "; break;
	}

	if (subclass) fprintf(f, "%s[%s::%s::%04lu] ", t, class, subclass, (unsigned long) pthread_self() % 10000);
	else fprintf(f, "%s[%s::%04lu] ", t, class, (unsigned long) pthread_self() % 10000);

	va_list args;
	va_start(args, format);

	vfprintf(f, format, args);

	va_end(args);

	if (settings.log) {
		FILE *l = fopen("rumble.log", "a");
		if (l) {
			time_t ti = time(NULL);
			struct tm tm;
			localtime_r(&ti, &tm);

			char ts[512];
			strftime(ts, sizeof(ts), "%c", &tm);

			fprintf(l, "(%s) ", ts);

			if (subclass) fprintf(l, "%s[%s::%s::%04lu] ", t, class, subclass, (unsigned long) pthread_self() % 10000);
			else fprintf(l, "%s[%s::%04lu] ", t, class, (unsigned long) pthread_self() % 10000);

			va_start(args, format);

			vfprintf(l, format, args);

			va_end(args);

			fclose(l);
		}
	}

	console_unlock();*/

	va_list args;
	va_start(args, format);

	console_print_message_v(type, class, subclass, format, args);

	va_end(args);
}
