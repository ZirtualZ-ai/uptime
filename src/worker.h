#ifndef UPTIME_WORKER_H
#define UPTIME_WORKER_H

#include "app.h"

int worker_start(AppState *state, pthread_t *thread);
void worker_stop(AppState *state, pthread_t thread);

#endif