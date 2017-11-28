#ifndef CONSOLE_H_
#define CONSOLE_H_

#include <stdarg.h>
#include <stdio.h>

enum {
	CONSOLE_MESSAGE,
	CONSOLE_WARNING,
	CONSOLE_ERROR,
	CONSOLE_DEBUG
};

#define _NONE NULL

void console_init();

void console_free();

void console_lock();

void console_unlock();

FILE * console_get_message_file();
FILE * console_get_warning_file();
FILE * console_get_error_file();
FILE * console_get_debug_file();

void console_print_v(int, const char *, va_list);

void console_print(int, const char *, ...);

#define print_message(f, a...) console_print(CONSOLE_MESSAGE, f, ## a)
#define print_warning(f, a...) console_print(CONSOLE_WARNING, f, ## a)
#define print_error(f, a...) console_print(CONSOLE_ERROR, f, ## a)
#define print_debug(f, a...) console_print(CONSOLE_DEBUG, f, ## a)

void console_print_message_v(int, const char *, const char *, const char *, va_list);

void console_print_message(int, const char *, const char *, const char *, ...);

#define console_message(c, s, f, a...) console_print_message(CONSOLE_MESSAGE, c, s, f, ## a)
#define console_warning(c, s, f, a...) console_print_message(CONSOLE_WARNING, c, s, f, ## a)
#define console_error(c, s, f, a...) console_print_message(CONSOLE_ERROR, c, s, f, ## a)
#define console_debug(c, s, f, a...) console_print_message(CONSOLE_DEBUG, c, s, f, ## a)

#endif /* CONSOLE_H_ */
