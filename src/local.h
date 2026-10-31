#ifndef UPTIME_LOCAL_H
#define UPTIME_LOCAL_H

#define UPTIME_TEXT 128

typedef struct {
    char ipv4[UPTIME_TEXT];
    char uptime[UPTIME_TEXT];
    char load[UPTIME_TEXT];
    char memory[UPTIME_TEXT];
    char hostname[UPTIME_TEXT];
} LocalStats;

void local_stats_read(LocalStats *stats);

#endif