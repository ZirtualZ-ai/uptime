#ifndef UPTIME_APP_H
#define UPTIME_APP_H

#include "local.h"

#include <arpa/inet.h>
#include <pthread.h>
#include <sqlite3.h>
#include <stddef.h>
#include <time.h>

#define UPTIME_MAX_SERVERS 32
#define UPTIME_MAX_SERVICES 128
#define UPTIME_MAX_DEVICES 256
#define UPTIME_MAX_EVENTS 80
#define UPTIME_NAME 64
#define UPTIME_ADDRESS 256
#define UPTIME_PATH 512
#define UPTIME_DETAIL 256

typedef struct {
    char name[UPTIME_NAME];
    char address[UPTIME_ADDRESS];
    char user[UPTIME_NAME];
    char wol_mac[32];
    char broadcast[UPTIME_ADDRESS];
    int port;
} ServerConfig;

typedef struct {
    char name[UPTIME_NAME];
    char server[UPTIME_NAME];
    char type[8];
    char target[UPTIME_ADDRESS];
    int port;
    int expected_status;
    unsigned int interval;
} ServiceConfig;

typedef struct {
    char subnet[64];
    char database[UPTIME_PATH];
    unsigned int scan_interval;
    unsigned int monitor_interval;
    int scan_enabled;
    size_t server_count;
    size_t service_count;
    ServerConfig servers[UPTIME_MAX_SERVERS];
    ServiceConfig services[UPTIME_MAX_SERVICES];
} AppConfig;

typedef struct {
    char name[UPTIME_NAME];
    char address[UPTIME_ADDRESS];
    int ping_ok;
    int ssh_ok;
    double cpu_percent;
    double memory_percent;
    double disk_percent;
    char uptime[UPTIME_DETAIL];
    char temperature[32];
    char detail[UPTIME_DETAIL];
    time_t checked_at;
    time_t history_at;
} ServerStatus;

typedef struct {
    char name[UPTIME_NAME];
    char server[UPTIME_NAME];
    char target[UPTIME_ADDRESS];
    int online;
    int http_status;
    double response_ms;
    unsigned int failures;
    time_t checked_at;
    time_t history_at;
} ServiceStatus;

typedef struct {
    char ip[INET_ADDRSTRLEN];
    char hostname[UPTIME_NAME];
    char mac[32];
    char vendor[UPTIME_NAME];
    time_t first_seen;
    time_t last_seen;
    int online;
} DeviceRecord;

typedef struct {
    time_t occurred_at;
    int level;
    char message[UPTIME_DETAIL];
} EventRecord;

typedef struct {
    LocalStats local;
    ServerStatus servers[UPTIME_MAX_SERVERS];
    ServiceStatus services[UPTIME_MAX_SERVICES];
    DeviceRecord devices[UPTIME_MAX_DEVICES];
    EventRecord events[UPTIME_MAX_EVENTS];
    size_t server_count;
    size_t service_count;
    size_t device_count;
    size_t event_count;
    size_t online_servers;
    size_t online_services;
    size_t online_devices;
    time_t last_scan;
    int scanning;
    int worker_ready;
    char scan_message[UPTIME_DETAIL];
    char notice[UPTIME_DETAIL];
} AppSnapshot;

typedef struct {
    AppConfig config;
    AppSnapshot snapshot;
    char config_file[UPTIME_PATH];
    sqlite3 *database;
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    int stopping;
    int scan_requested;
} AppState;

#endif