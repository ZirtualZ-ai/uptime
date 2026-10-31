#include "worker.h"

#include "db.h"
#include "process.h"

#include <ctype.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#define SCAN_OUTPUT_SIZE 65536

static void set_notice(AppState *state, const char *message)
{
    pthread_mutex_lock(&state->mutex);
    snprintf(state->snapshot.notice, sizeof(state->snapshot.notice), "%s", message);
    pthread_mutex_unlock(&state->mutex);
}

static void add_event(AppState *state, sqlite3 *database, int level, const char *message)
{
    EventRecord event;
    memset(&event, 0, sizeof(event));
    event.occurred_at = time(NULL);
    event.level = level;
    snprintf(event.message, sizeof(event.message), "%s", message);
    if (database != NULL) db_add_event(database, level, message);
    pthread_mutex_lock(&state->mutex);
    if (state->snapshot.event_count == UPTIME_MAX_EVENTS) state->snapshot.event_count--;
    memmove(&state->snapshot.events[1], &state->snapshot.events[0],
            state->snapshot.event_count * sizeof(state->snapshot.events[0]));
    state->snapshot.events[0] = event;
    state->snapshot.event_count++;
    pthread_mutex_unlock(&state->mutex);
}

static int parse_metric(const char *text, const char *key, double *value)
{
    char needle[32];
    const char *found;
    snprintf(needle, sizeof(needle), "%s=", key);
    found = strstr(text, needle);
    return found != NULL && sscanf(found + strlen(needle), "%lf", value) == 1;
}

static void parse_remote_stats(ServerStatus *status, const char *text)
{
    const char *uptime = strstr(text, "uptime=");
    const char *temperature = strstr(text, "temperature=");
    status->cpu_percent = -1.0;
    status->memory_percent = -1.0;
    status->disk_percent = -1.0;
    if (!parse_metric(text, "cpu", &status->cpu_percent)) status->cpu_percent = -1.0;
    if (!parse_metric(text, "memory", &status->memory_percent)) status->memory_percent = -1.0;
    if (!parse_metric(text, "disk", &status->disk_percent)) status->disk_percent = -1.0;
    if (uptime != NULL) sscanf(uptime + 7, "%255[^\n]", status->uptime);
    if (temperature != NULL) sscanf(temperature + 11, "%31[^\n]", status->temperature);
}

static void check_server(AppState *state, sqlite3 *database, size_t index)
{
    static const char remote_command[] =
        "set -- $(awk '/^cpu /{print $2+$3+$4+$5+$6+$7+$8+$9, $5+$6}' /proc/stat); t1=$1; i1=$2; sleep 1; "
        "set -- $(awk '/^cpu /{print $2+$3+$4+$5+$6+$7+$8+$9, $5+$6}' /proc/stat); t2=$1; i2=$2; "
        "awk -v t1=$t1 -v t2=$t2 -v i1=$i1 -v i2=$i2 'BEGIN{if(t2>t1) printf \"cpu=%.1f\\n\",100*(1-(i2-i1)/(t2-t1)); else print \"cpu=0\\n\"}'; "
        "awk '/MemTotal:/{t=$2}/MemAvailable:/{a=$2}END{if(t>0)printf \"memory=%.1f\\n\",100*(t-a)/t}' /proc/meminfo; "
        "df -P / | awk 'NR==2{gsub(/%/,\"\",$5);printf \"disk=%s\\n\",$5}'; "
        "printf 'uptime='; cut -d. -f1 /proc/uptime; "
        "for f in /sys/class/thermal/thermal_zone*/temp; do if test -r \"$f\"; then "
        "awk '{if($1>1000)printf \"temperature=%.1fC\\n\",$1/1000;else printf \"temperature=%sC\\n\",$1}' \"$f\"; break; fi; done";
    const ServerConfig *configuration = &state->config.servers[index];
    ServerStatus previous;
    ServerStatus current;
    char output[4096];
    char destination[UPTIME_ADDRESS + UPTIME_NAME + 8];
    char port[16];
    int ssh_result;
    char *ping_arguments[] = { "ping", "-n", "-c", "1", "-W", "2", (char *)configuration->address, NULL };
    char *ssh_arguments[16];
    size_t argument = 0;

    pthread_mutex_lock(&state->mutex);
    previous = state->snapshot.servers[index];
    pthread_mutex_unlock(&state->mutex);
    current = previous;
    current.ping_ok = process_capture(ping_arguments, 4, output, sizeof(output)) == 0;
    snprintf(destination, sizeof(destination), "%s@%s", configuration->user, configuration->address);
    snprintf(port, sizeof(port), "%d", configuration->port);
    ssh_arguments[argument++] = "ssh";
    ssh_arguments[argument++] = "-n";
    ssh_arguments[argument++] = "-oBatchMode=yes";
    ssh_arguments[argument++] = "-oConnectTimeout=5";
    ssh_arguments[argument++] = "-oConnectionAttempts=1";
    ssh_arguments[argument++] = "-oStrictHostKeyChecking=accept-new";
    ssh_arguments[argument++] = "-p";
    ssh_arguments[argument++] = port;
    ssh_arguments[argument++] = destination;
    ssh_arguments[argument++] = (char *)remote_command;
    ssh_arguments[argument] = NULL;
    ssh_result = process_capture(ssh_arguments, 10, output, sizeof(output));
    current.ssh_ok = ssh_result == 0;
    current.checked_at = time(NULL);
    current.detail[0] = '\0';
    if (current.ssh_ok) parse_remote_stats(&current, output);
    else snprintf(current.detail, sizeof(current.detail), "%s", current.ping_ok ? "SSH unavailable" : "host unreachable");

    if ((previous.checked_at == 0 && !current.ping_ok) ||
        (previous.checked_at != 0 && previous.ping_ok != current.ping_ok)) {
        char message[UPTIME_DETAIL];
        snprintf(message, sizeof(message), "%s is %s", configuration->name, current.ping_ok ? "reachable" : "offline");
        add_event(state, database, current.ping_ok ? 0 : 2, message);
    }
    if ((previous.checked_at == 0 && !current.ssh_ok) ||
        (previous.checked_at != 0 && previous.ssh_ok != current.ssh_ok)) {
        char message[UPTIME_DETAIL];
        snprintf(message, sizeof(message), "%s SSH %s", configuration->name, current.ssh_ok ? "recovered" : "failed");
        add_event(state, database, current.ssh_ok ? 0 : 2, message);
    }
    if (database != NULL && (previous.checked_at == 0 || previous.ping_ok != current.ping_ok ||
        previous.ssh_ok != current.ssh_ok || current.checked_at - previous.history_at >= 900)) {
        db_record_server(database, &current);
        current.history_at = current.checked_at;
    }
    pthread_mutex_lock(&state->mutex);
    state->snapshot.servers[index] = current;
    state->snapshot.online_servers = 0;
    for (size_t i = 0; i < state->snapshot.server_count; i++) {
        if (state->snapshot.servers[i].ping_ok) state->snapshot.online_servers++;
    }
    pthread_mutex_unlock(&state->mutex);
}

static int parse_nmap_line(char *line, DeviceRecord *device)
{
    char *host = strstr(line, "Host: ");
    char *mac;
    char *vendor;
    char *close;
    char address[INET_ADDRSTRLEN] = "";
    char hostname[UPTIME_NAME] = "";
    struct in_addr parsed;
    if (host == NULL || strstr(host, "Status: Up") == NULL) return 0;
    host += 6;
    if (sscanf(host, "%15s", address) != 1 || inet_pton(AF_INET, address, &parsed) != 1) return 0;
    memset(device, 0, sizeof(*device));
    snprintf(device->ip, sizeof(device->ip), "%s", address);
    if (*host != '\0') {
        char *open = strchr(host, '(');
        close = open != NULL ? strchr(open + 1, ')') : NULL;
        if (open != NULL && close != NULL && close > open + 1) {
            size_t length = (size_t)(close - open - 1);
            if (length >= sizeof(hostname)) length = sizeof(hostname) - 1;
            memcpy(hostname, open + 1, length);
            hostname[length] = '\0';
            if (strcmp(hostname, "") != 0) snprintf(device->hostname, sizeof(device->hostname), "%s", hostname);
        }
    }
    mac = strstr(host, "MAC Address: ");
    if (mac != NULL) {
        mac += strlen("MAC Address: ");
        if (sscanf(mac, "%17s", device->mac) != 1) device->mac[0] = '\0';
        vendor = strchr(mac, '(');
        close = vendor != NULL ? strchr(vendor + 1, ')') : NULL;
        if (vendor != NULL && close != NULL && close > vendor + 1) {
            size_t length = (size_t)(close - vendor - 1);
            if (length >= sizeof(device->vendor)) length = sizeof(device->vendor) - 1;
            memcpy(device->vendor, vendor + 1, length);
            device->vendor[length] = '\0';
        }
    }
    return 1;
}

static int previously_online(AppState *state, const char *ip, int *known)
{
    int online = 0;
    pthread_mutex_lock(&state->mutex);
    *known = 0;
    for (size_t i = 0; i < state->snapshot.device_count; i++) {
        if (strcmp(state->snapshot.devices[i].ip, ip) == 0) {
            *known = 1;
            online = state->snapshot.devices[i].online;
            break;
        }
    }
    pthread_mutex_unlock(&state->mutex);
    return online;
}

static void perform_scan(AppState *state, sqlite3 *database)
{
    char *arguments[] = { "nmap", "-sn", "-oG", "-", state->config.subnet, NULL };
    char *output = malloc(SCAN_OUTPUT_SIZE);
    char *line;
    char *save;
    char message[UPTIME_DETAIL];
    sqlite3_int64 scan_id = 0;
    size_t count = 0;
    char seen[UPTIME_MAX_DEVICES][INET_ADDRSTRLEN];
    size_t seen_count = 0;
    DeviceRecord previous_devices[UPTIME_MAX_DEVICES];
    size_t previous_count;
    int result;

    pthread_mutex_lock(&state->mutex);
    previous_count = state->snapshot.device_count;
    memcpy(previous_devices, state->snapshot.devices, previous_count * sizeof(previous_devices[0]));
    state->snapshot.scanning = 1;
    snprintf(state->snapshot.scan_message, sizeof(state->snapshot.scan_message), "Scanning %s with Nmap", state->config.subnet);
    pthread_mutex_unlock(&state->mutex);
    if (database != NULL) db_begin_scan(database, &scan_id);
    if (output == NULL) result = -1;
    else result = process_capture(arguments, 120, output, SCAN_OUTPUT_SIZE);
    if (result != 0) {
        if (database != NULL) db_finish_scan(database, scan_id, 0, 0);
        snprintf(message, sizeof(message), "Network scan failed (Nmap missing or timed out)");
        add_event(state, database, 2, message);
        set_notice(state, message);
        pthread_mutex_lock(&state->mutex);
        state->snapshot.scanning = 0;
        state->snapshot.last_scan = time(NULL);
        snprintf(state->snapshot.scan_message, sizeof(state->snapshot.scan_message), "%s", message);
        pthread_mutex_unlock(&state->mutex);
        free(output);
        return;
    }
    if (database != NULL) {
        db_mark_devices_offline(database);
    }
    for (line = strtok_r(output, "\n", &save); line != NULL; line = strtok_r(NULL, "\n", &save)) {
        DeviceRecord device;
        if (!parse_nmap_line(line, &device)) continue;
        int known;
        int online_before = previously_online(state, device.ip, &known);
        if (database != NULL) {
            int was_new;
            int was_offline;
            db_upsert_device(database, &device, &was_new, &was_offline);
        }
        if (seen_count < UPTIME_MAX_DEVICES) {
            snprintf(seen[seen_count++], sizeof(seen[0]), "%s", device.ip);
        }
        if (!known || !online_before) {
            snprintf(message, sizeof(message), "%s device discovered%s%s", device.ip,
                     device.hostname[0] ? " (" : "", device.hostname[0] ? device.hostname : "");
            if (device.hostname[0]) {
                size_t length = strlen(message);
                if (length + 1 < sizeof(message)) snprintf(message + length, sizeof(message) - length, ")");
            }
            add_event(state, database, known ? 0 : 1, message);
        }
        count++;
    }
    for (size_t i = 0; i < previous_count; i++) {
        int still_seen = 0;
        if (!previous_devices[i].online) continue;
        for (size_t j = 0; j < seen_count; j++) {
            if (strcmp(previous_devices[i].ip, seen[j]) == 0) {
                still_seen = 1;
                break;
            }
        }
        if (!still_seen) {
            snprintf(message, sizeof(message), "Device %.15s disappeared from the network", previous_devices[i].ip);
            add_event(state, database, 1, message);
        }
    }
    if (database != NULL) db_finish_scan(database, scan_id, 1, count);
    pthread_mutex_lock(&state->mutex);
    if (database != NULL) db_load_devices(database, &state->snapshot);
    else {
        state->snapshot.device_count = 0;
        state->snapshot.online_devices = 0;
    }
    state->snapshot.last_scan = time(NULL);
    state->snapshot.scanning = 0;
    snprintf(state->snapshot.scan_message, sizeof(state->snapshot.scan_message), "Last scan found %zu device(s)", count);
    pthread_mutex_unlock(&state->mutex);
    snprintf(message, sizeof(message), "Network scan complete: %zu device(s) found", count);
    add_event(state, database, 0, message);
    free(output);
}

static void check_service(AppState *state, sqlite3 *database, size_t index)
{
    const ServiceConfig *configuration = &state->config.services[index];
    ServiceStatus previous;
    ServiceStatus current;
    long long started = 0;
    long long finished = 0;
    char output[256];
    int valid;

    pthread_mutex_lock(&state->mutex);
    previous = state->snapshot.services[index];
    pthread_mutex_unlock(&state->mutex);
    current = previous;
    started = process_monotonic_ms();
    current.http_status = 0;
    current.response_ms = 0.0;
    if (strcmp(configuration->type, "tcp") == 0) {
        current.online = process_tcp_probe(configuration->target, configuration->port, 2500);
    } else {
        char *arguments[] = { "curl", "--silent", "--show-error", "--output", "/dev/null", "--connect-timeout", "4", "--max-time", "8", "--write-out", "%{http_code} %{time_total}", (char *)configuration->target, NULL };
        int result = process_capture(arguments, 10, output, sizeof(output));
        double seconds = 0.0;
        current.http_status = atoi(output);
        if (sscanf(output, "%d %lf", &current.http_status, &seconds) != 2) seconds = 0.0;
        current.response_ms = seconds * 1000.0;
        current.online = result == 0 && (configuration->expected_status == 0 ? current.http_status >= 200 && current.http_status < 400 : current.http_status == configuration->expected_status);
    }
    finished = process_monotonic_ms();
    if (strcmp(configuration->type, "tcp") == 0) current.response_ms = (double)(finished - started);
    current.checked_at = time(NULL);
    if (current.online) current.failures = 0;
    else current.failures++;
    valid = previous.checked_at != 0;
    if ((!valid && !current.online) || (valid && previous.online != current.online)) {
        char message[UPTIME_DETAIL];
        snprintf(message, sizeof(message), "Service %s %s", configuration->name, current.online ? "recovered" : "failed");
        add_event(state, database, current.online ? 0 : 2, message);
    }
    if (database != NULL && (previous.checked_at == 0 || previous.online != current.online ||
        current.checked_at - previous.history_at >= 900)) {
        db_record_service(database, &current);
        current.history_at = current.checked_at;
    }
    pthread_mutex_lock(&state->mutex);
    state->snapshot.services[index] = current;
    state->snapshot.online_services = 0;
    for (size_t i = 0; i < state->snapshot.service_count; i++) {
        if (state->snapshot.services[i].online) state->snapshot.online_services++;
    }
    pthread_mutex_unlock(&state->mutex);
}

static void *worker_main(void *argument)
{
    AppState *state = argument;
    sqlite3 *database = NULL;
    char error[UPTIME_DETAIL];
    time_t next_monitor = 0;
    time_t next_scan = 0;
    time_t next_services[UPTIME_MAX_SERVICES] = {0};

    if (db_open(&database, state->config.database, error, sizeof(error)) != 0) {
        set_notice(state, error);
    } else {
        AppSnapshot initial = {0};
        db_load_devices(database, &initial);
        db_load_events(database, &initial);
        initial.last_scan = db_load_last_scan(database);
        pthread_mutex_lock(&state->mutex);
        memcpy(state->snapshot.devices, initial.devices, sizeof(initial.devices));
        memcpy(state->snapshot.events, initial.events, sizeof(initial.events));
        state->snapshot.device_count = initial.device_count;
        state->snapshot.event_count = initial.event_count;
        state->snapshot.online_devices = initial.online_devices;
        state->snapshot.last_scan = initial.last_scan;
        pthread_mutex_unlock(&state->mutex);
    }
    pthread_mutex_lock(&state->mutex);
    state->snapshot.worker_ready = 1;
    pthread_mutex_unlock(&state->mutex);

    for (;;) {
        time_t now = time(NULL);
        int scan_now;
        pthread_mutex_lock(&state->mutex);
        if (state->stopping) {
            pthread_mutex_unlock(&state->mutex);
            break;
        }
        scan_now = state->scan_requested;
        state->scan_requested = 0;
        pthread_mutex_unlock(&state->mutex);

        if (now >= next_monitor) {
            LocalStats local;
            local_stats_read(&local);
            pthread_mutex_lock(&state->mutex);
            state->snapshot.local = local;
            pthread_mutex_unlock(&state->mutex);
            for (size_t i = 0; i < state->config.server_count; i++) check_server(state, database, i);
            next_monitor = now + state->config.monitor_interval;
        }
        for (size_t i = 0; i < state->config.service_count; i++) {
            if (now >= next_services[i]) {
                check_service(state, database, i);
                next_services[i] = now + state->config.services[i].interval;
            }
        }
        if (scan_now || (state->config.scan_enabled && now >= next_scan)) {
            perform_scan(state, database);
            next_scan = time(NULL) + state->config.scan_interval;
        }

        pthread_mutex_lock(&state->mutex);
        if (!state->stopping) {
            struct timespec wake;
            clock_gettime(CLOCK_REALTIME, &wake);
            wake.tv_sec += 1;
            pthread_cond_timedwait(&state->condition, &state->mutex, &wake);
        }
        pthread_mutex_unlock(&state->mutex);
    }
    if (database != NULL) sqlite3_close(database);
    return NULL;
}

int worker_start(AppState *state, pthread_t *thread)
{
    return pthread_create(thread, NULL, worker_main, state);
}

void worker_stop(AppState *state, pthread_t thread)
{
    pthread_mutex_lock(&state->mutex);
    state->stopping = 1;
    pthread_cond_broadcast(&state->condition);
    pthread_mutex_unlock(&state->mutex);
    pthread_join(thread, NULL);
}