#ifndef AM_HTTP_REACTOR_H
#define AM_HTTP_REACTOR_H
#include <stddef.h>
typedef struct am_reactor am_reactor;
/* Caller owns API access; only the worker owns sockets/TLS. DNS is synchronous
 * on the submitting caller for this prototype. IDs never reuse a live slot. */
int am_reactor_set_max_threads(int maximum);
int am_reactor_get_max_threads(void);
int am_reactor_worker_count(void);
/* Diagnostic owner ID; remains stable through completion. */
int am_reactor_request_worker(am_reactor *, int id);
am_reactor *am_reactor_open(void);
int am_reactor_set_limit(am_reactor *,size_t limit);
int am_reactor_submit(am_reactor *, const char *host, int port, const char *path,
                      int tls, int timeout_ms, char *error, size_t error_size);
int am_reactor_ready(am_reactor *, int id);
int am_reactor_wait_any(am_reactor *, int timeout_ms);
/* wait=0 returns 0 while pending; 1 transfers malloc'ed raw bytes; -1 error.
 * A terminal take consumes the slot, including errors. */
int am_reactor_take(am_reactor *, int id, int wait, unsigned char **data,
                    size_t *size, char *error, size_t error_size);
void am_reactor_cancel(am_reactor *, int id);
void am_reactor_close(am_reactor *);
#endif
