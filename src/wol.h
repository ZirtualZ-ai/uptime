#ifndef UPTIME_WOL_H
#define UPTIME_WOL_H

int wol_send(const char *mac_address, const char *broadcast_address);

#endif