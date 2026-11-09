/* HTTPS requests (RetroAchievements' server), each on a thread of its own so the game never
 * waits: WinHTTP on Windows; on Linux the system's libcurl, loaded when first needed (SteamOS
 * and the desktop distributions have it), so the game needs no library of its own for it.
 * Answers are handed over on the main thread, from http_poll. */
#pragma once

#include <stdbool.h>
#include <stddef.h>

/* status: the HTTP status, or 0 when no answer came (body then holds the reason) */
typedef void (*HttpDone)(int status, const char *body, size_t len, void *userdata);

/* POST post_data (application/x-www-form-urlencoded; NULL for a GET) to url. False when the
 * request cannot be made at all (no libcurl): done is not called, http_error says why. */
bool http_post(const char *url, const char *post_data, const char *user_agent, HttpDone done, void *userdata);
/* Main thread, once a frame: calls done for every request that has finished. */
void http_poll(void);
/* Requests still running. */
int http_pending(void);
const char *http_error(void);
