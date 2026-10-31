#ifndef UPTIME_WORKER_H
#define UPTIME_WORKER_H

#include "app.h"

void *worker_main(void *argument);
void worker_stop(AppState *state, pthread_t thread);

#endif