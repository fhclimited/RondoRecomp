/* HTTPS requests (RetroAchievements' server, updates from GitHub), each on a thread of its own
 * so the game never waits (Setup's downloads block): WinHTTP on Windows; on Linux the system's libcurl, loaded when first needed (SteamOS
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
/* Blocking requests, on the calling thread (Setup; a worker thread in a window).
 * progress: bytes so far and of how many (expected, 0 unknown); returning false cancels.
 * Both return the HTTP status (after redirects), or 0 when no answer came; err says what
 * went wrong when it is not 200. http_get_text's body is NUL-terminated: free() it. */
typedef bool (*HttpProgress)(long long done, long long total, void *userdata);
int http_get_file(const char *url, const char *user_agent, const char *path, long long expected, HttpProgress progress,
                  void *userdata, char *err, size_t errlen);
int http_get_text(const char *url, const char *user_agent, char **body, size_t *len, char *err, size_t errlen);
/* Main thread, once a frame: calls done for every request that has finished. */
void http_poll(void);
/* Requests still running. */
int http_pending(void);
const char *http_error(void);
