#include "wol.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int parse_mac(const char *text, unsigned char mac[6])
{
    size_t length = strlen(text);
    char separator;
    if (length != 17) return -1;
    separator = text[2];
    if (separator != ':' && separator != '-') return -1;
    for (int i = 0; i < 6; i++) {
        unsigned int byte;
        char *end;
        char octet[3] = { text[i * 3], text[i * 3 + 1], '\0' };
        if (!isxdigit((unsigned char)octet[0]) || !isxdigit((unsigned char)octet[1])) return -1;
        if (i < 5 && text[i * 3 + 2] != separator) return -1;
        errno = 0;
        byte = (unsigned int)strtoul(octet, &end, 16);
        if (errno != 0 || end != octet + 2 || byte > 255) return -1;
        mac[i] = (unsigned char)byte;
    }
    return 0;
}

int wol_send(const char *mac_address, const char *broadcast_address)
{
    unsigned char mac[6];
    unsigned char packet[102];
    struct sockaddr_in destination;
    int descriptor;

    if (parse_mac(mac_address, mac) != 0) {
        errno = EINVAL;
        return -1;
    }
    memset(packet, 0xff, 6);
    for (int repeat = 0; repeat < 16; repeat++) memcpy(packet + 6 + repeat * 6, mac, 6);
    memset(&destination, 0, sizeof(destination));
    destination.sin_family = AF_INET;
    destination.sin_port = htons(9);
    if (inet_pton(AF_INET, broadcast_address, &destination.sin_addr) != 1) {
        errno = EINVAL;
        return -1;
    }
    descriptor = socket(AF_INET, SOCK_DGRAM, 0);
    if (descriptor < 0) return -1;
    {
        int enabled = 1;
        if (setsockopt(descriptor, SOL_SOCKET, SO_BROADCAST, &enabled, sizeof(enabled)) != 0 ||
            sendto(descriptor, packet, sizeof(packet), 0, (struct sockaddr *)&destination, sizeof(destination)) != (ssize_t)sizeof(packet)) {
            int saved_error = errno;
            close(descriptor);
            errno = saved_error;
            return -1;
        }
    }
    close(descriptor);
    return 0;
}