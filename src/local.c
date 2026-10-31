#include "local.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

static void read_uptime(char *output, size_t output_size)
{
    FILE *file = fopen("/proc/uptime", "r");
    double seconds = 0.0;
    unsigned long days;
    unsigned long hours;
    unsigned long minutes;

    if (file == NULL || fscanf(file, "%lf", &seconds) != 1) {
        if (file != NULL) {
            fclose(file);
        }
        snprintf(output, output_size, "unavailable");
        return;
    }
    fclose(file);

    days = (unsigned long)seconds / 86400UL;
    hours = ((unsigned long)seconds / 3600UL) % 24UL;
    minutes = ((unsigned long)seconds / 60UL) % 60UL;
    if (days > 0) {
        snprintf(output, output_size, "%lu d %lu h %lu m", days, hours, minutes);
    } else {
        snprintf(output, output_size, "%lu h %lu m", hours, minutes);
    }
}

static void read_load(char *output, size_t output_size)
{
    FILE *file = fopen("/proc/loadavg", "r");
    double one = 0.0;
    double five = 0.0;
    double fifteen = 0.0;

    if (file == NULL || fscanf(file, "%lf %lf %lf", &one, &five, &fifteen) != 3) {
        if (file != NULL) {
            fclose(file);
        }
        snprintf(output, output_size, "unavailable");
        return;
    }
    fclose(file);
    snprintf(output, output_size, "%.2f / %.2f / %.2f", one, five, fifteen);
}

static void read_memory(char *output, size_t output_size)
{
    FILE *file = fopen("/proc/meminfo", "r");
    char line[256];
    unsigned long total = 0;
    unsigned long available = 0;

    if (file == NULL) {
        snprintf(output, output_size, "unavailable");
        return;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        if (sscanf(line, "MemTotal: %lu kB", &total) == 1) {
            continue;
        }
        if (sscanf(line, "MemAvailable: %lu kB", &available) == 1) {
            break;
        }
    }
    fclose(file);
    if (total == 0) {
        snprintf(output, output_size, "unavailable");
        return;
    }
    if (available == 0) {
        available = total;
    }
    snprintf(output, output_size, "%.0f / %.0f MiB (%.0f%% used)",
             (double)(total - available) / 1024.0,
             (double)total / 1024.0,
             100.0 * (double)(total - available) / (double)total);
}

static void read_ipv4(char *output, size_t output_size)
{
    struct ifaddrs *interfaces = NULL;
    struct ifaddrs *current;

    snprintf(output, output_size, "not connected");
    if (getifaddrs(&interfaces) != 0) {
        return;
    }
    for (current = interfaces; current != NULL; current = current->ifa_next) {
        char address[INET_ADDRSTRLEN];
        struct sockaddr_in *ipv4;

        if (current->ifa_addr == NULL || current->ifa_addr->sa_family != AF_INET ||
            (current->ifa_flags & IFF_LOOPBACK) != 0) {
            continue;
        }
        ipv4 = (struct sockaddr_in *)current->ifa_addr;
        if (inet_ntop(AF_INET, &ipv4->sin_addr, address, sizeof(address)) != NULL) {
            snprintf(output, output_size, "%s  (%s)", address, current->ifa_name);
            break;
        }
    }
    freeifaddrs(interfaces);
}

void local_stats_read(LocalStats *stats)
{
    memset(stats, 0, sizeof(*stats));
    if (gethostname(stats->hostname, sizeof(stats->hostname) - 1) != 0) {
        snprintf(stats->hostname, sizeof(stats->hostname), "unknown");
    }
    read_ipv4(stats->ipv4, sizeof(stats->ipv4));
    read_uptime(stats->uptime, sizeof(stats->uptime));
    read_load(stats->load, sizeof(stats->load));
    read_memory(stats->memory, sizeof(stats->memory));
}