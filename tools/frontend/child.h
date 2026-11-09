/* Child processes whose output is read while they run (Setup runs the compiler and, for an
 * update, the new Setup; the game runs Setup's update). On Windows they start without a
 * console window: a console program started from a windowed one (or from one started that
 * way) would otherwise open one over the game. Standard output and error come together. */
#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct Child Child;

/* args: the program, then its arguments, then NULL. NULL if it cannot start (err says why). */
Child *child_start(const char *const *args, char *err, size_t errlen);
/* What it has written so far, without waiting: > 0 bytes, 0 nothing yet, -1 no more output. */
int child_read(Child *c, char *buf, int n);
/* Whether it has ended (*code: its exit code); block waits for that. */
bool child_wait(Child *c, bool block, int *code);
/* Frees the handle (the process is not stopped). */
void child_free(Child *c);
