/* Setup's parts shared between its files (main.c: import, recompile, build; install.c:
 * updates and putting files in place). */
#pragma once

#include <stdbool.h>
#include <stddef.h>

extern char g_err[512];
extern void (*setup_line_hook)(const char *text);
extern void (*setup_progress_hook)(const char *what, int pct);

void say(const char *fmt, ...);
void join(char *out, size_t n, const char *a, const char *b);
void remove_tree(const char *path);
bool make_dirs_for(const char *file);

/* Runs a program to its end, its output passed on as Setup's own (progress lines as
 * progress). False if it cannot start or fails (g_err says which). */
bool setup_run(const char *const *args);
/* Puts the file `fresh` at `dest` (both in DIR or below it): a file already there, even a
 * program running now, is moved out of the way into DIR/update/old first, and back again
 * if `fresh` cannot take its place. */
bool setup_replace(const char *dir, const char *fresh, const char *dest);

/* rondo_setup --update [--check]: see install.c. 0 updated, 3 already the latest, 1 failed. */
int setup_update(const char *dir, bool check_only);
