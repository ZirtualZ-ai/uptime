#ifndef UPTIME_DB_H
#define UPTIME_DB_H

#include "app.h"

#include <sqlite3.h>

int db_open(sqlite3 **database, const char *path, char *error, size_t error_size);
int db_load_devices(sqlite3 *database, AppSnapshot *snapshot);
int db_load_events(sqlite3 *database, AppSnapshot *snapshot);
time_t db_load_last_scan(sqlite3 *database);
int db_add_event(sqlite3 *database, int level, const char *message);
int db_record_server(sqlite3 *database, const ServerStatus *status);
int db_record_service(sqlite3 *database, const ServiceStatus *status);
int db_begin_scan(sqlite3 *database, sqlite3_int64 *scan_id);
int db_finish_scan(sqlite3 *database, sqlite3_int64 scan_id, int success, size_t device_count);
int db_mark_devices_offline(sqlite3 *database);
int db_upsert_device(sqlite3 *database, const DeviceRecord *device, int *was_new, int *was_offline);

#endif