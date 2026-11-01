#include "db.h"

#include <stdio.h>
#include <string.h>

#define DB_SCHEMA_VERSION 2

static const char schema_sql[] =
    "CREATE TABLE IF NOT EXISTS devices ("
    "ip TEXT PRIMARY KEY, hostname TEXT NOT NULL DEFAULT '', mac TEXT NOT NULL DEFAULT '',"
    "vendor TEXT NOT NULL DEFAULT '', first_seen INTEGER NOT NULL, last_seen INTEGER NOT NULL, online INTEGER NOT NULL DEFAULT 0);"
    "CREATE TABLE IF NOT EXISTS scans (id INTEGER PRIMARY KEY, started_at INTEGER NOT NULL, finished_at INTEGER, success INTEGER NOT NULL DEFAULT 0, device_count INTEGER NOT NULL DEFAULT 0);"
    "CREATE TABLE IF NOT EXISTS events (id INTEGER PRIMARY KEY, occurred_at INTEGER NOT NULL, level INTEGER NOT NULL, message TEXT NOT NULL);"
    "CREATE TABLE IF NOT EXISTS server_history (id INTEGER PRIMARY KEY, checked_at INTEGER NOT NULL, name TEXT NOT NULL, ping_ok INTEGER NOT NULL, ssh_ok INTEGER NOT NULL, detail TEXT NOT NULL);"
    "CREATE TABLE IF NOT EXISTS service_history (id INTEGER PRIMARY KEY, checked_at INTEGER NOT NULL, name TEXT NOT NULL, online INTEGER NOT NULL, http_status INTEGER NOT NULL, response_ms REAL NOT NULL, failures INTEGER NOT NULL);"
    "CREATE INDEX IF NOT EXISTS events_time_idx ON events(occurred_at DESC);"
    "CREATE INDEX IF NOT EXISTS devices_seen_idx ON devices(last_seen DESC);";

int db_open(sqlite3 **database, const char *path, char *error, size_t error_size)
{
    int version = 0;
    char *message = NULL;
    sqlite3_stmt *statement = NULL;

    if (sqlite3_open_v2(path, database, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, NULL) != SQLITE_OK) {
        snprintf(error, error_size, "SQLite: %s", *database != NULL ? sqlite3_errmsg(*database) : "open failed");
        if (*database != NULL) sqlite3_close(*database);
        *database = NULL;
        return -1;
    }
    sqlite3_busy_timeout(*database, 3000);
    sqlite3_exec(*database, "PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL; PRAGMA foreign_keys=ON;", NULL, NULL, NULL);
    if (sqlite3_prepare_v2(*database, "PRAGMA user_version", -1, &statement, NULL) == SQLITE_OK && sqlite3_step(statement) == SQLITE_ROW) {
        version = sqlite3_column_int(statement, 0);
    }
    sqlite3_finalize(statement);
    if (version > DB_SCHEMA_VERSION) {
        snprintf(error, error_size, "database schema %d is newer than this program", version);
        sqlite3_close(*database);
        *database = NULL;
        return -1;
    }
    if (sqlite3_exec(*database, "BEGIN IMMEDIATE", NULL, NULL, &message) != SQLITE_OK ||
        sqlite3_exec(*database, schema_sql, NULL, NULL, &message) != SQLITE_OK ||
        (version < 2 && sqlite3_exec(*database,
            "ALTER TABLE devices ADD COLUMN open_ports TEXT NOT NULL DEFAULT '';"
            "ALTER TABLE devices ADD COLUMN ssh_user TEXT NOT NULL DEFAULT '';"
            "ALTER TABLE devices ADD COLUMN ssh_port INTEGER NOT NULL DEFAULT 0;"
            "ALTER TABLE devices ADD COLUMN ssh_open INTEGER NOT NULL DEFAULT 0;"
            "ALTER TABLE devices ADD COLUMN ports_known INTEGER NOT NULL DEFAULT 0;"
            "ALTER TABLE devices ADD COLUMN managed INTEGER NOT NULL DEFAULT 0;"
            "ALTER TABLE devices ADD COLUMN last_ssh_success INTEGER NOT NULL DEFAULT 0;",
            NULL, NULL, &message) != SQLITE_OK) ||
        sqlite3_exec(*database, "PRAGMA user_version=2; COMMIT", NULL, NULL, &message) != SQLITE_OK) {
        snprintf(error, error_size, "SQLite migration: %s", message != NULL ? message : sqlite3_errmsg(*database));
        sqlite3_free(message);
        sqlite3_exec(*database, "ROLLBACK", NULL, NULL, NULL);
        sqlite3_close(*database);
        *database = NULL;
        return -1;
    }
    sqlite3_free(message);
    sqlite3_exec(*database,
        "DELETE FROM server_history WHERE checked_at < CAST(strftime('%s','now') AS INTEGER)-2592000;"
        "DELETE FROM service_history WHERE checked_at < CAST(strftime('%s','now') AS INTEGER)-2592000;"
        "DELETE FROM events WHERE occurred_at < CAST(strftime('%s','now') AS INTEGER)-15552000;"
        "DELETE FROM scans WHERE started_at < CAST(strftime('%s','now') AS INTEGER)-31536000;",
        NULL, NULL, NULL);
    if (error_size > 0) error[0] = '\0';
    return 0;
}

int db_load_devices(sqlite3 *database, AppSnapshot *snapshot)
{
    sqlite3_stmt *statement = NULL;
    int result;
    snapshot->device_count = 0;
    snapshot->online_devices = 0;
    if (sqlite3_prepare_v2(database, "SELECT ip,hostname,mac,vendor,open_ports,ssh_user,ssh_port,ssh_open,ports_known,managed,first_seen,last_seen,last_ssh_success,online FROM devices ORDER BY managed DESC,online DESC,last_seen DESC LIMIT ?", -1, &statement, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int(statement, 1, UPTIME_MAX_DEVICES);
    while ((result = sqlite3_step(statement)) == SQLITE_ROW && snapshot->device_count < UPTIME_MAX_DEVICES) {
        DeviceRecord *device = &snapshot->devices[snapshot->device_count++];
        snprintf(device->ip, sizeof(device->ip), "%s", sqlite3_column_text(statement, 0));
        snprintf(device->hostname, sizeof(device->hostname), "%s", sqlite3_column_text(statement, 1));
        snprintf(device->mac, sizeof(device->mac), "%s", sqlite3_column_text(statement, 2));
        snprintf(device->vendor, sizeof(device->vendor), "%s", sqlite3_column_text(statement, 3));
        snprintf(device->open_ports, sizeof(device->open_ports), "%s", sqlite3_column_text(statement, 4));
        snprintf(device->ssh_user, sizeof(device->ssh_user), "%s", sqlite3_column_text(statement, 5));
        device->ssh_port = sqlite3_column_int(statement, 6);
        device->ssh_open = sqlite3_column_int(statement, 7);
        device->ports_known = sqlite3_column_int(statement, 8);
        device->managed = sqlite3_column_int(statement, 9);
        device->first_seen = (time_t)sqlite3_column_int64(statement, 10);
        device->last_seen = (time_t)sqlite3_column_int64(statement, 11);
        device->last_ssh_success = (time_t)sqlite3_column_int64(statement, 12);
        device->online = sqlite3_column_int(statement, 13);
        if (device->online) snapshot->online_devices++;
    }
    sqlite3_finalize(statement);
    return result == SQLITE_DONE ? 0 : -1;
}

int db_load_events(sqlite3 *database, AppSnapshot *snapshot)
{
    sqlite3_stmt *statement = NULL;
    int result;
    snapshot->event_count = 0;
    if (sqlite3_prepare_v2(database, "SELECT occurred_at,level,message FROM events ORDER BY id DESC LIMIT ?", -1, &statement, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int(statement, 1, UPTIME_MAX_EVENTS);
    while ((result = sqlite3_step(statement)) == SQLITE_ROW && snapshot->event_count < UPTIME_MAX_EVENTS) {
        EventRecord *event = &snapshot->events[snapshot->event_count++];
        event->occurred_at = (time_t)sqlite3_column_int64(statement, 0);
        event->level = sqlite3_column_int(statement, 1);
        snprintf(event->message, sizeof(event->message), "%s", sqlite3_column_text(statement, 2));
    }
    sqlite3_finalize(statement);
    return result == SQLITE_DONE ? 0 : -1;
}

time_t db_load_last_scan(sqlite3 *database)
{
    sqlite3_stmt *statement = NULL;
    time_t result = 0;
    if (sqlite3_prepare_v2(database, "SELECT COALESCE(MAX(finished_at),0) FROM scans", -1, &statement, NULL) == SQLITE_OK &&
        sqlite3_step(statement) == SQLITE_ROW) {
        result = (time_t)sqlite3_column_int64(statement, 0);
    }
    sqlite3_finalize(statement);
    return result;
}

int db_add_event(sqlite3 *database, int level, const char *message)
{
    sqlite3_stmt *statement = NULL;
    int ok = sqlite3_prepare_v2(database, "INSERT INTO events(occurred_at,level,message) VALUES(?,?,?)", -1, &statement, NULL) == SQLITE_OK;
    if (ok) {
        sqlite3_bind_int64(statement, 1, (sqlite3_int64)time(NULL));
        sqlite3_bind_int(statement, 2, level);
        sqlite3_bind_text(statement, 3, message, -1, SQLITE_TRANSIENT);
        ok = sqlite3_step(statement) == SQLITE_DONE;
    }
    sqlite3_finalize(statement);
    return ok ? 0 : -1;
}

static int record_history(sqlite3 *database, const char *sql, const char *name, int first, int second, double value, unsigned int failures, const char *detail)
{
    sqlite3_stmt *statement = NULL;
    int ok = sqlite3_prepare_v2(database, sql, -1, &statement, NULL) == SQLITE_OK;
    if (ok) {
        sqlite3_bind_int64(statement, 1, (sqlite3_int64)time(NULL));
        sqlite3_bind_text(statement, 2, name, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(statement, 3, first);
        sqlite3_bind_int(statement, 4, second);
        if (detail != NULL) sqlite3_bind_text(statement, 5, detail, -1, SQLITE_TRANSIENT);
        else {
            sqlite3_bind_double(statement, 5, value);
            sqlite3_bind_int(statement, 6, (int)failures);
        }
        ok = sqlite3_step(statement) == SQLITE_DONE;
    }
    sqlite3_finalize(statement);
    return ok ? 0 : -1;
}

int db_record_server(sqlite3 *database, const ServerStatus *status)
{
    return record_history(database, "INSERT INTO server_history(checked_at,name,ping_ok,ssh_ok,detail) VALUES(?,?,?,?,?)",
                          status->name, status->ping_ok, status->ssh_ok, 0.0, 0, status->detail);
}

int db_record_service(sqlite3 *database, const ServiceStatus *status)
{
    sqlite3_stmt *statement = NULL;
    int ok = sqlite3_prepare_v2(database, "INSERT INTO service_history(checked_at,name,online,http_status,response_ms,failures) VALUES(?,?,?,?,?,?)", -1, &statement, NULL) == SQLITE_OK;
    if (ok) {
        sqlite3_bind_int64(statement, 1, (sqlite3_int64)time(NULL));
        sqlite3_bind_text(statement, 2, status->name, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(statement, 3, status->online);
        sqlite3_bind_int(statement, 4, status->http_status);
        sqlite3_bind_double(statement, 5, status->response_ms);
        sqlite3_bind_int(statement, 6, (int)status->failures);
        ok = sqlite3_step(statement) == SQLITE_DONE;
    }
    sqlite3_finalize(statement);
    return ok ? 0 : -1;
}

int db_begin_scan(sqlite3 *database, sqlite3_int64 *scan_id)
{
    sqlite3_stmt *statement = NULL;
    int ok = sqlite3_prepare_v2(database, "INSERT INTO scans(started_at) VALUES(?)", -1, &statement, NULL) == SQLITE_OK;
    if (ok) {
        sqlite3_bind_int64(statement, 1, (sqlite3_int64)time(NULL));
        ok = sqlite3_step(statement) == SQLITE_DONE;
        *scan_id = sqlite3_last_insert_rowid(database);
    }
    sqlite3_finalize(statement);
    return ok ? 0 : -1;
}

int db_finish_scan(sqlite3 *database, sqlite3_int64 scan_id, int success, size_t device_count)
{
    sqlite3_stmt *statement = NULL;
    int ok = sqlite3_prepare_v2(database, "UPDATE scans SET finished_at=?,success=?,device_count=? WHERE id=?", -1, &statement, NULL) == SQLITE_OK;
    if (ok) {
        sqlite3_bind_int64(statement, 1, (sqlite3_int64)time(NULL));
        sqlite3_bind_int(statement, 2, success);
        sqlite3_bind_int64(statement, 3, (sqlite3_int64)device_count);
        sqlite3_bind_int64(statement, 4, scan_id);
        ok = sqlite3_step(statement) == SQLITE_DONE;
    }
    sqlite3_finalize(statement);
    return ok ? 0 : -1;
}

int db_mark_devices_offline(sqlite3 *database)
{
    return sqlite3_exec(database, "UPDATE devices SET online=0", NULL, NULL, NULL) == SQLITE_OK ? 0 : -1;
}

int db_upsert_device(sqlite3 *database, const DeviceRecord *device, int *was_new, int *was_offline)
{
    sqlite3_stmt *statement = NULL;
    int exists = 0;
    if (sqlite3_prepare_v2(database, "SELECT online FROM devices WHERE ip=?", -1, &statement, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(statement, 1, device->ip, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(statement) == SQLITE_ROW) {
        exists = 1;
        *was_offline = sqlite3_column_int(statement, 0) == 0;
    } else *was_offline = 0;
    sqlite3_finalize(statement);
    *was_new = !exists;
    statement = NULL;
    if (sqlite3_prepare_v2(database, "INSERT INTO devices(ip,hostname,mac,vendor,open_ports,ssh_open,first_seen,last_seen,online) VALUES(?,?,?,?,?,?, ?,?,1) ON CONFLICT(ip) DO UPDATE SET hostname=CASE WHEN excluded.hostname='' THEN devices.hostname ELSE excluded.hostname END,mac=CASE WHEN excluded.mac='' THEN devices.mac ELSE excluded.mac END,vendor=CASE WHEN excluded.vendor='' THEN devices.vendor ELSE excluded.vendor END,open_ports=CASE WHEN excluded.open_ports='' THEN devices.open_ports ELSE excluded.open_ports END,ssh_open=CASE WHEN excluded.open_ports='' THEN devices.ssh_open ELSE excluded.ssh_open END,last_seen=excluded.last_seen,online=1", -1, &statement, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(statement, 1, device->ip, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, device->hostname, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 3, device->mac, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 4, device->vendor, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 5, device->open_ports, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(statement, 6, device->ssh_open);
    sqlite3_bind_int64(statement, 7, (sqlite3_int64)time(NULL));
    sqlite3_bind_int64(statement, 8, (sqlite3_int64)time(NULL));
    {
        int ok = sqlite3_step(statement) == SQLITE_DONE;
        sqlite3_finalize(statement);
        return ok ? 0 : -1;
    }
}

int db_enroll_device(sqlite3 *database, const DeviceRecord *device)
{
    sqlite3_stmt *statement = NULL;
    const char *sql = "INSERT INTO devices(ip,hostname,mac,vendor,open_ports,ssh_user,ssh_port,ssh_open,managed,first_seen,last_seen,last_ssh_success,online) "
                      "VALUES(?,?,?,?,?,?,?,?,1,?,?,?,1) ON CONFLICT(ip) DO UPDATE SET "
                      "hostname=CASE WHEN excluded.hostname='' THEN devices.hostname ELSE excluded.hostname END,"
                      "mac=CASE WHEN excluded.mac='' THEN devices.mac ELSE excluded.mac END,"
                      "vendor=CASE WHEN excluded.vendor='' THEN devices.vendor ELSE excluded.vendor END,"
                      "ssh_user=excluded.ssh_user,ssh_port=excluded.ssh_port,ssh_open=1,managed=1,"
                      "last_seen=excluded.last_seen,last_ssh_success=excluded.last_ssh_success,online=1";
    int ok = sqlite3_prepare_v2(database, sql, -1, &statement, NULL) == SQLITE_OK;

    if (ok) {
        sqlite3_int64 now = (sqlite3_int64)time(NULL);
        sqlite3_bind_text(statement, 1, device->ip, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement, 2, device->hostname, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement, 3, device->mac, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement, 4, device->vendor, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement, 5, device->open_ports, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement, 6, device->ssh_user, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(statement, 7, device->ssh_port);
        sqlite3_bind_int(statement, 8, 1);
        sqlite3_bind_int64(statement, 9, now);
        sqlite3_bind_int64(statement, 10, now);
        sqlite3_bind_int64(statement, 11, now);
        ok = sqlite3_step(statement) == SQLITE_DONE;
    }
    sqlite3_finalize(statement);
    return ok ? 0 : -1;
}

int db_mark_ssh_success(sqlite3 *database, const char *ip)
{
    sqlite3_stmt *statement = NULL;
    int ok = sqlite3_prepare_v2(database,
        "UPDATE devices SET last_ssh_success=?,last_seen=?,online=1 WHERE ip=?", -1, &statement, NULL) == SQLITE_OK;
    if (ok) {
        sqlite3_int64 now = (sqlite3_int64)time(NULL);
        sqlite3_bind_int64(statement, 1, now);
        sqlite3_bind_int64(statement, 2, now);
        sqlite3_bind_text(statement, 3, ip, -1, SQLITE_TRANSIENT);
        ok = sqlite3_step(statement) == SQLITE_DONE;
    }
    sqlite3_finalize(statement);
    return ok ? 0 : -1;
}

int db_reset_scan_ports(sqlite3 *database)
{
    return sqlite3_exec(database, "UPDATE devices SET open_ports='',ssh_open=0,ports_known=1,ssh_port=CASE WHEN managed=1 THEN ssh_port ELSE 0 END WHERE online=1", NULL, NULL, NULL) == SQLITE_OK ? 0 : -1;
}

int db_update_device_ports(sqlite3 *database, const char *ip, const char *ports, int ssh_open, int ssh_port)
{
    sqlite3_stmt *statement = NULL;
    int ok = sqlite3_prepare_v2(database,
        "UPDATE devices SET open_ports=?,ssh_open=?,ssh_port=?,ports_known=1 WHERE ip=?", -1, &statement, NULL) == SQLITE_OK;
    if (ok) {
        sqlite3_bind_text(statement, 1, ports, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(statement, 2, ssh_open);
        sqlite3_bind_int(statement, 3, ssh_port);
        sqlite3_bind_text(statement, 4, ip, -1, SQLITE_TRANSIENT);
        ok = sqlite3_step(statement) == SQLITE_DONE;
    }
    sqlite3_finalize(statement);
    return ok ? 0 : -1;
}