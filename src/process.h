#ifndef UPTIME_PROCESS_H
#define UPTIME_PROCESS_H

#include <stddef.h>

int process_capture(char *const arguments[], unsigned int timeout_seconds, char *output, size_t output_size);
int process_tcp_probe(const char *host, int port, unsigned int timeout_ms);
long long process_monotonic_ms(void);

#endif