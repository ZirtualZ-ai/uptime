#include "ui.h"

#include "wol.h"

#include <ncurses.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define UPTIME_VERSION "0.1.0"

static int color_enabled;

static void put_text(int row, int column, const char *text, int width, int color, int bold)
{
    if (row < 0 || column < 0 || width <= 0) return;
    if (color_enabled && color > 0) attron(COLOR_PAIR(color));
    if (bold) attron(A_BOLD);
    mvaddnstr(row, column, text, width);
    if (bold) attroff(A_BOLD);
    if (color_enabled && color > 0) attroff(COLOR_PAIR(color));
}

static void format_time(time_t value, char *output, size_t output_size)
{
    struct tm local_time;
    if (value == 0) {
        snprintf(output, output_size, "never");
    } else if (localtime_r(&value, &local_time) != NULL) {
        strftime(output, output_size, "%Y-%m-%d %H:%M:%S", &local_time);
    } else {
        snprintf(output, output_size, "unknown");
    }
}

static void format_short_time(time_t value, char *output, size_t output_size)
{
    struct tm local_time;
    if (value == 0) {
        snprintf(output, output_size, "--:--:--");
    } else if (localtime_r(&value, &local_time) != NULL) {
        strftime(output, output_size, "%H:%M:%S", &local_time);
    } else {
        snprintf(output, output_size, "--:--:--");
    }
}

static void draw_header(const AppSnapshot *snapshot, int columns)
{
    time_t now = time(NULL);
    struct tm local_time;
    char timestamp[64] = "time unavailable";
    char title[128];

    if (localtime_r(&now, &local_time) != NULL) {
        strftime(timestamp, sizeof(timestamp), "%a %Y-%m-%d  %H:%M:%S", &local_time);
    }
    snprintf(title, sizeof(title), " UPTIME  /  HOMELAB OPERATIONS CONSOLE  v%s", UPTIME_VERSION);
    attron(A_REVERSE);
    mvhline(0, 0, ' ', columns);
    put_text(0, 0, title, columns - 1, 1, 1);
    attroff(A_REVERSE);
    put_text(1, 2, timestamp, 30, 0, 0);
    put_text(1, 35, snapshot->local.hostname, 24, 0, 1);
    put_text(1, 62, "1-7 Views", columns - 64, 5, 0);
}

static void draw_footer(int rows, int columns)
{
    const char *shortcuts = " 1-7 Views   Up/Down Select   W Wake   R Scan   Q Quit ";
    if (rows < 1) return;
    attron(A_REVERSE);
    mvhline(rows - 1, 0, ' ', columns);
    put_text(rows - 1, 0, shortcuts, columns - 1, 0, 0);
    attroff(A_REVERSE);
}

static int status_color(int online)
{
    return online ? 2 : 4;
}

static void draw_overview(const AppState *state, const AppSnapshot *snapshot, int columns)
{
    char row[UPTIME_DETAIL];
    char last_scan[64];
    format_time(snapshot->last_scan, last_scan, sizeof(last_scan));
    if (LINES < 24) {
        char event_time[32];
        put_text(3, 2, "LOCAL", columns - 4, 1, 1);
        snprintf(row, sizeof(row), "%-18.18s  %s", snapshot->local.hostname, snapshot->local.ipv4);
        put_text(4, 4, row, columns - 6, 0, 0);
        snprintf(row, sizeof(row), "Network %s  uptime %.18s  memory %.35s",
                 strcmp(snapshot->local.ipv4, "not connected") == 0 ? "DOWN" : "UP",
                 snapshot->local.uptime, snapshot->local.memory);
        put_text(5, 4, row, columns - 6, 0, 0);
        snprintf(row, sizeof(row), "LAN %.20s  devices %zu/%zu  last scan %.19s",
             state->config_file[0] ? state->config.subnet : "not configured",
             snapshot->online_devices, snapshot->device_count, last_scan);
        put_text(6, 4, row, columns - 6, snapshot->scanning ? 3 : 0, 0);
        snprintf(row, sizeof(row), "Servers %zu/%zu online    Services %zu/%zu online",
                 snapshot->online_servers, snapshot->server_count,
                 snapshot->online_services, snapshot->service_count);
        put_text(7, 4, row, columns - 6, 1, 0);
        if (snapshot->server_count > 0) {
            const ServerStatus *server = &snapshot->servers[0];
            snprintf(row, sizeof(row), "%-14.14s %s  SSH %s",
                     server->name, server->checked_at == 0 ? "WAIT" : server->ping_ok ? "UP" : "DOWN",
                     server->checked_at == 0 ? "WAIT" : server->ssh_ok ? "UP" : "DOWN");
            put_text(8, 4, row, columns - 6, server->checked_at == 0 ? 1 : status_color(server->ping_ok), 0);
        }
        if (snapshot->service_count > 0) {
            snprintf(row, sizeof(row), "%-14.14s %s  %.0f ms", snapshot->services[0].name,
                     snapshot->services[0].checked_at == 0 ? "WAIT" : snapshot->services[0].online ? "UP" : "DOWN",
                     snapshot->services[0].response_ms);
            put_text(9, 4, row, columns - 6, snapshot->services[0].checked_at == 0 ? 1 : status_color(snapshot->services[0].online), 0);
        }
        put_text(10, 2, "RECENT EVENTS", columns - 4, 1, 1);
        if (snapshot->event_count > 0) {
            format_time(snapshot->events[0].occurred_at, event_time, sizeof(event_time));
            snprintf(row, sizeof(row), "%.19s  %.75s", event_time, snapshot->events[0].message);
            put_text(11, 4, row, columns - 6, snapshot->events[0].level >= 2 ? 4 : 3, 0);
        }
        return;
    }

    put_text(3, 2, "LOCAL", columns - 4, 1, 1);
    snprintf(row, sizeof(row), "%-18.18s  IPv4 %s", snapshot->local.hostname, snapshot->local.ipv4);
    put_text(4, 4, row, columns - 6, 0, 0);
    snprintf(row, sizeof(row), "Network %-4s  uptime %-18.18s  load %.30s",
             strcmp(snapshot->local.ipv4, "not connected") == 0 ? "DOWN" : "UP",
             snapshot->local.uptime, snapshot->local.load);
    put_text(5, 4, row, columns - 6, 0, 0);
    snprintf(row, sizeof(row), "Memory %s", snapshot->local.memory);
    put_text(6, 4, row, columns - 6, 0, 0);
    snprintf(row, sizeof(row), "NETWORK  %s  devices %zu/%zu  last scan %s%s",
             state->config_file[0] ? state->config.subnet : "not configured",
             snapshot->online_devices, snapshot->device_count, last_scan,
             snapshot->scanning ? "  SCANNING" : "");
    put_text(7, 2, row, columns - 4, snapshot->scanning ? 3 : 1, 1);
    snprintf(row, sizeof(row), "SERVERS  %zu/%zu online", snapshot->online_servers, snapshot->server_count);
    put_text(8, 2, row, columns - 4, 1, 1);
    for (size_t i = 0; i < snapshot->server_count && i < 2; i++) {
        const ServerStatus *server = &snapshot->servers[i];
        snprintf(row, sizeof(row), "%-14.14s %-16.16s ping:%s ssh:%s CPU:%s RAM:%s DISK:%s",
                 server->name, server->address, server->checked_at == 0 ? "wait" : server->ping_ok ? "up" : "down",
                 server->checked_at == 0 ? "wait" : server->ssh_ok ? "up" : "down",
                 server->cpu_percent >= 0 ? "ok" : "--", server->memory_percent >= 0 ? "ok" : "--",
                 server->disk_percent >= 0 ? "ok" : "--");
        put_text(9 + (int)i, 4, row, columns - 6, server->checked_at == 0 ? 1 : status_color(server->ping_ok), 0);
    }
    put_text(11, 2, "SERVICES", columns - 4, 1, 1);
    snprintf(row, sizeof(row), "%zu/%zu online", snapshot->online_services, snapshot->service_count);
    put_text(11, 13, row, columns - 15, 0, 0);
    for (size_t i = 0; i < snapshot->service_count && i < 3; i++) {
        snprintf(row, sizeof(row), "%-18.18s %-26.26s %s  %.0f ms", snapshot->services[i].name,
                 snapshot->services[i].target,
                 snapshot->services[i].checked_at == 0 ? "WAIT" : snapshot->services[i].online ? "UP" : "DOWN",
                 snapshot->services[i].response_ms);
        put_text(12 + (int)i, 4, row, columns - 6,
                 snapshot->services[i].checked_at == 0 ? 1 : status_color(snapshot->services[i].online), 0);
    }
    put_text(15, 2, "RECENT EVENTS", columns - 4, 1, 1);
    for (size_t i = 0; i < snapshot->event_count && i < 2; i++) {
        char event_time[32];
        format_time(snapshot->events[i].occurred_at, event_time, sizeof(event_time));
        snprintf(row, sizeof(row), "%.19s  %.100s", event_time, snapshot->events[i].message);
        put_text(16 + (int)i, 4, row, columns - 6,
                 snapshot->events[i].level >= 2 ? 4 : snapshot->events[i].level == 1 ? 3 : 2, 0);
    }
}

static void draw_servers(const AppSnapshot *snapshot, int columns, int selected)
{
    int visible = (LINES - 8) / 2;
    int first = selected >= visible ? selected - visible + 1 : 0;
    char row[UPTIME_DETAIL];
    put_text(3, 2, "SERVER HEALTH  (W sends a magic packet to the selected server)", columns - 4, 1, 1);
    for (int i = first; i < (int)snapshot->server_count && i < first + visible; i++) {
        const ServerStatus *server = &snapshot->servers[i];
        char cpu[16];
        char memory[16];
        char disk[16];
        snprintf(cpu, sizeof(cpu), server->cpu_percent < 0 ? "--" : "%5.1f%%", server->cpu_percent);
        snprintf(memory, sizeof(memory), server->memory_percent < 0 ? "--" : "%5.1f%%", server->memory_percent);
        snprintf(disk, sizeof(disk), server->disk_percent < 0 ? "--" : "%5.1f%%", server->disk_percent);
        snprintf(row, sizeof(row), "%-14.14s %-16.16s ping %-4s SSH %-4s CPU %6s RAM %6s disk %6s",
             server->name, server->address,
             server->checked_at == 0 ? "WAIT" : server->ping_ok ? "UP" : "DOWN",
             server->checked_at == 0 ? "WAIT" : server->ssh_ok ? "UP" : "DOWN", cpu, memory, disk);
        put_text(5 + (i - first) * 2, 3, row, columns - 6, server->checked_at == 0 ? 1 : status_color(server->ping_ok), i == selected);
        if (i == selected && 6 + (i - first) * 2 < LINES - 2) {
            snprintf(row, sizeof(row), "  uptime %-20.20s temperature %-12.12s %.120s",
                     server->uptime[0] ? server->uptime : "--", server->temperature[0] ? server->temperature : "--", server->detail);
            put_text(6 + (i - first) * 2, 5, row, columns - 8, 5, 0);
        }
    }
    if (snapshot->server_count == 0) put_text(5, 3, "No servers configured. Edit the configuration file.", columns - 6, 3, 0);
}

static void draw_services(const AppState *state, const AppSnapshot *snapshot, int columns, int selected)
{
    int visible = LINES - 7;
    int first = selected >= visible ? selected - visible + 1 : 0;
    char row[UPTIME_DETAIL];
    put_text(3, 2, "SERVICE CHECKS  TCP and HTTP(S) checks run at configured intervals", columns - 4, 1, 1);
    for (int i = first; i < (int)snapshot->service_count && i < first + visible; i++) {
        const ServiceStatus *status = &snapshot->services[i];
        const ServiceConfig *configuration = &state->config.services[i];
        snprintf(row, sizeof(row), "%-18.18s %-6s %-28.28s:%-5d %-5s %6.0f ms failures:%u",
             status->name, configuration->type, status->target, configuration->port,
             status->checked_at == 0 ? "WAIT" : status->online ? "UP" : "DOWN", status->response_ms, status->failures);
        put_text(5 + i - first, 3, row, columns - 6, status->checked_at == 0 ? 1 : status_color(status->online), i == selected);
    }
    if (snapshot->service_count == 0) put_text(5, 3, "No services configured.", columns - 6, 3, 0);
}

static void draw_devices(const AppSnapshot *snapshot, int columns, int history, int selected)
{
    int visible = LINES - 9;
    int first = selected >= visible ? selected - visible + 1 : 0;
    char row[UPTIME_DETAIL];
    put_text(3, 2, history ? "DEVICE HISTORY" : "NETWORK DISCOVERY", columns - 4, 1, 1);
    put_text(4, 3, snapshot->scanning ? snapshot->scan_message : "Use R to run a configured-subnet Nmap discovery scan.", columns - 6, 3, 0);
    put_text(5, 3, "STATE  IP              HOSTNAME               MAC                MANUFACTURER", columns - 6, 5, 1);
    for (int i = first; i < (int)snapshot->device_count && i < first + visible; i++) {
        const DeviceRecord *device = &snapshot->devices[i];
        snprintf(row, sizeof(row), "%-5s  %-15s %-22s %-18s %s", device->online ? "UP" : "DOWN",
                 device->ip, device->hostname[0] ? device->hostname : "-",
                 device->mac[0] ? device->mac : "-", device->vendor[0] ? device->vendor : "-");
        put_text(6 + i - first, 3, row, columns - 6, device->online ? 2 : (history ? 5 : 4), i == selected);
    }
    if (history && selected >= 0 && (size_t)selected < snapshot->device_count) {
        char first_seen[32];
        char last_seen[32];
        const DeviceRecord *device = &snapshot->devices[selected];
        format_time(device->first_seen, first_seen, sizeof(first_seen));
        format_time(device->last_seen, last_seen, sizeof(last_seen));
        snprintf(row, sizeof(row), "Selected: first seen %s  last seen %s  manufacturer %s",
                 first_seen, last_seen, device->vendor[0] ? device->vendor : "unknown");
        put_text(LINES - 3, 3, row, columns - 6, 5, 0);
    }
    if (snapshot->device_count == 0) put_text(6, 3, "No device history yet.", columns - 6, 5, 0);
}

static void draw_events(const AppSnapshot *snapshot, int columns)
{
    char row[UPTIME_DETAIL + 48];
    put_text(3, 2, "RECENT EVENTS", columns - 4, 1, 1);
    for (size_t i = 0; i < snapshot->event_count && i < (size_t)(LINES - 7); i++) {
        char timestamp[32];
        format_time(snapshot->events[i].occurred_at, timestamp, sizeof(timestamp));
        snprintf(row, sizeof(row), "%-19s  %s", timestamp, snapshot->events[i].message);
        put_text(5 + (int)i, 3, row, columns - 6, snapshot->events[i].level >= 2 ? 4 : snapshot->events[i].level == 1 ? 3 : 2, 0);
    }
    if (snapshot->event_count == 0) put_text(5, 3, "No events recorded.", columns - 6, 5, 0);
}

static void draw_config(const AppState *state, int columns)
{
    int row = 3;
    char text[UPTIME_DETAIL];
    put_text(row++, 2, "CONFIGURATION", columns - 4, 1, 1);
    snprintf(text, sizeof(text), "File       %.220s", state->config_file[0] ? state->config_file : "defaults (local-only)");
    put_text(row++, 4, text, columns - 6, 0, 0);
    snprintf(text, sizeof(text), "Database   %.220s", state->config.database);
    put_text(row++, 4, text, columns - 6, 0, 0);
    snprintf(text, sizeof(text), "Subnet     %s   scan every %u seconds (%s)", state->config.subnet,
             state->config.scan_interval, state->config.scan_enabled ? "enabled" : "disabled");
    put_text(row++, 4, text, columns - 6, 0, 0);
    snprintf(text, sizeof(text), "Monitoring every %u seconds", state->config.monitor_interval);
    put_text(row++, 4, text, columns - 6, 0, 0);
    row++;
    put_text(row++, 4, "Configuration is edited in the human-readable INI file; restart to apply changes.", columns - 6, 5, 0);
    put_text(row, 4, "SSH uses BatchMode and key authentication; no passwords are stored.", columns - 6, 5, 0);
}

static int compact_line(int row, int columns, const char *text, int color, int bold)
{
    int width = columns - 1;
    if (row < 0 || row >= LINES || width <= 0) return row;
    put_text(row, 0, text, width, color, bold);
    return row + 1;
}

static void draw_compact_view(const AppState *state, const AppSnapshot *snapshot,
                              int screen, int selected, int rows, int columns)
{
    char text[UPTIME_DETAIL];
    char timestamp[32];
    int row = 0;
    int content_end = rows - 1;

    if (rows <= 0 || columns <= 0) return;
    if (rows == 1) {
        compact_line(0, columns, "UPTIME  Q quit", 1, 1);
        return;
    }

    snprintf(text, sizeof(text), "UPTIME %s  [%d/7]", UPTIME_VERSION, screen);
    row = compact_line(row, columns, text, 1, 1);
    if (row < content_end) {
        time_t now = time(NULL);
        struct tm local_time;
        if (localtime_r(&now, &local_time) != NULL) {
            strftime(timestamp, sizeof(timestamp), "%H:%M:%S", &local_time);
        } else {
            snprintf(timestamp, sizeof(timestamp), "--:--:--");
        }
        snprintf(text, sizeof(text), "%s  %s", timestamp, snapshot->local.hostname);
        row = compact_line(row, columns, text, 0, 0);
    }

    if (screen == 1) {
        if (row < content_end) {
            snprintf(text, sizeof(text), "NET %s  %s", strcmp(snapshot->local.ipv4, "not connected") == 0 ? "DOWN" : "UP", snapshot->local.ipv4);
            row = compact_line(row, columns, text, strcmp(snapshot->local.ipv4, "not connected") == 0 ? 4 : 2, 0);
        }
        if (row < content_end) {
            snprintf(text, sizeof(text), "UP %.32s  LOAD %.32s", snapshot->local.uptime, snapshot->local.load);
            row = compact_line(row, columns, text, 0, 0);
        }
        if (row < content_end) {
            snprintf(text, sizeof(text), "MEM %s", snapshot->local.memory);
            row = compact_line(row, columns, text, 0, 0);
        }
        if (row < content_end) {
            snprintf(text, sizeof(text), "LAN %s %zu/%zu %s", state->config_file[0] ? state->config.subnet : "off",
                     snapshot->online_devices, snapshot->device_count,
                     snapshot->scanning ? "SCANNING" : "");
            row = compact_line(row, columns, text, snapshot->scanning ? 3 : 0, 0);
        }
        if (row < content_end) {
            snprintf(text, sizeof(text), "HOSTS %zu/%zu  SVC %zu/%zu",
                     snapshot->online_servers, snapshot->server_count,
                     snapshot->online_services, snapshot->service_count);
            row = compact_line(row, columns, text, 1, 0);
        }
        if (snapshot->event_count > 0 && row < content_end) {
            format_short_time(snapshot->events[0].occurred_at, timestamp, sizeof(timestamp));
            snprintf(text, sizeof(text), "EVENT %.8s %.150s", timestamp, snapshot->events[0].message);
            row = compact_line(row, columns, text, snapshot->events[0].level >= 2 ? 4 : 3, 0);
        }
    } else if (screen == 2) {
        for (size_t i = 0; i < snapshot->server_count && row < content_end; i++) {
            const ServerStatus *server = &snapshot->servers[i];
            snprintf(text, sizeof(text), "%c %.18s %.15s ping:%s ssh:%s",
                     (int)i == selected ? '>' : ' ', server->name, server->address,
                     server->checked_at == 0 ? "wait" : server->ping_ok ? "up" : "down",
                     server->checked_at == 0 ? "wait" : server->ssh_ok ? "up" : "down");
            row = compact_line(row, columns, text, server->checked_at == 0 ? 1 : status_color(server->ping_ok), (int)i == selected);
        }
        if (snapshot->server_count == 0 && row < content_end) row = compact_line(row, columns, "No servers configured", 3, 0);
    } else if (screen == 3) {
        for (size_t i = 0; i < snapshot->service_count && row < content_end; i++) {
            const ServiceStatus *service = &snapshot->services[i];
            snprintf(text, sizeof(text), "%c %.18s %s %.0fms fail:%u",
                     (int)i == selected ? '>' : ' ', service->name,
                     service->checked_at == 0 ? "wait" : service->online ? "up" : "down",
                     service->response_ms, service->failures);
            row = compact_line(row, columns, text, service->checked_at == 0 ? 1 : status_color(service->online), (int)i == selected);
        }
        if (snapshot->service_count == 0 && row < content_end) row = compact_line(row, columns, "No services configured", 3, 0);
    } else if (screen == 4 || screen == 5) {
        for (size_t i = 0; i < snapshot->device_count && row < content_end; i++) {
            const DeviceRecord *device = &snapshot->devices[i];
            snprintf(text, sizeof(text), "%c %s %.18s %.16s",
                     (int)i == selected ? '>' : ' ', device->online ? "UP" : "--",
                     device->ip, device->hostname[0] ? device->hostname : device->mac);
            row = compact_line(row, columns, text, device->online ? 2 : 5, (int)i == selected);
        }
        if (snapshot->device_count == 0 && row < content_end) row = compact_line(row, columns, "No device history", 5, 0);
    } else if (screen == 6) {
        for (size_t i = 0; i < snapshot->event_count && row < content_end; i++) {
            format_short_time(snapshot->events[i].occurred_at, timestamp, sizeof(timestamp));
            snprintf(text, sizeof(text), "%.8s %.150s", timestamp, snapshot->events[i].message);
            row = compact_line(row, columns, text,
                               snapshot->events[i].level >= 2 ? 4 : snapshot->events[i].level == 1 ? 3 : 2, 0);
        }
        if (snapshot->event_count == 0 && row < content_end) row = compact_line(row, columns, "No recent events", 5, 0);
    } else if (screen == 7) {
        snprintf(text, sizeof(text), "CFG %.150s", state->config_file[0] ? state->config_file : "local-only");
        if (row < content_end) row = compact_line(row, columns, text, 0, 0);
        snprintf(text, sizeof(text), "DB %.150s", state->config.database);
        if (row < content_end) row = compact_line(row, columns, text, 0, 0);
        snprintf(text, sizeof(text), "SCAN %u sec  MON %u sec", state->config.scan_interval, state->config.monitor_interval);
        if (row < content_end) row = compact_line(row, columns, text, 0, 0);
    }

    if (content_end > 0) {
        snprintf(text, sizeof(text), "1-7 views  R scan  W wake  Q quit");
        attron(A_REVERSE);
        mvhline(rows - 1, 0, ' ', columns);
        put_text(rows - 1, 0, text, columns - 1, 0, 0);
        attroff(A_REVERSE);
    }
}

static void draw_view(const AppState *state, const AppSnapshot *snapshot, int screen, int selected)
{
    int rows;
    int columns;
    getmaxyx(stdscr, rows, columns);
    erase();
    if (rows < 24 || columns < 80) {
        draw_compact_view(state, snapshot, screen, selected, rows, columns);
        refresh();
        return;
    }
    draw_header(snapshot, columns);
    switch (screen) {
    case 1: draw_overview(state, snapshot, columns); break;
    case 2: draw_servers(snapshot, columns, selected); break;
    case 3: draw_services(state, snapshot, columns, selected); break;
    case 4: draw_devices(snapshot, columns, 0, selected); break;
    case 5: draw_devices(snapshot, columns, 1, selected); break;
    case 6: draw_events(snapshot, columns); break;
    case 7: draw_config(state, columns); break;
    default: break;
    }
    if (snapshot->notice[0] != '\0') put_text(rows - 2, 2, snapshot->notice, columns - 4, 3, 0);
    draw_footer(rows, columns);
    refresh();
}

static void request_scan(AppState *state)
{
    pthread_mutex_lock(&state->mutex);
    if (state->config_file[0] == '\0') {
        snprintf(state->snapshot.notice, sizeof(state->snapshot.notice), "Load an explicit config file before scanning a network");
        pthread_mutex_unlock(&state->mutex);
        return;
    }
    state->scan_requested = 1;
    pthread_cond_signal(&state->condition);
    snprintf(state->snapshot.notice, sizeof(state->snapshot.notice), "Network scan queued for %s", state->config.subnet);
    pthread_mutex_unlock(&state->mutex);
}

static void wake_server(AppState *state, const AppSnapshot *snapshot, int screen, int selected)
{
    const ServerConfig *server;
    const char *broadcast;
    char notice[UPTIME_DETAIL];
    if (screen != 2) {
        snprintf(notice, sizeof(notice), "Open Servers and select a machine before waking it");
    } else if (selected < 0 || (size_t)selected >= state->config.server_count) {
        snprintf(notice, sizeof(notice), "Select a configured server first");
    } else {
        server = &state->config.servers[selected];
        broadcast = server->broadcast[0] ? server->broadcast : "255.255.255.255";
        if (server->wol_mac[0] == '\0') {
            snprintf(notice, sizeof(notice), "%s has no wol_mac configured", server->name);
        } else if (wol_send(server->wol_mac, broadcast) != 0) {
            snprintf(notice, sizeof(notice), "Wake packet failed for %s", server->name);
        } else {
            snprintf(notice, sizeof(notice), "Magic packet sent to %s; current ping status: %s (boot not confirmed)",
                     server->name, snapshot->servers[selected].ping_ok ? "online" : "offline");
        }
    }
    pthread_mutex_lock(&state->mutex);
    snprintf(state->snapshot.notice, sizeof(state->snapshot.notice), "%s", notice);
    pthread_mutex_unlock(&state->mutex);
}

int ui_run(AppState *state)
{
    int screen = 1;
    int selected = 0;
    int key;

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    timeout(1000);
    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(1, COLOR_CYAN, -1);
        init_pair(2, COLOR_GREEN, -1);
        init_pair(3, COLOR_YELLOW, -1);
        init_pair(4, COLOR_RED, -1);
        init_pair(5, COLOR_WHITE, -1);
        color_enabled = 1;
    }

    for (;;) {
        AppSnapshot snapshot;
        LocalStats local;
        pthread_mutex_lock(&state->mutex);
        snapshot = state->snapshot;
        pthread_mutex_unlock(&state->mutex);
        local_stats_read(&local);
        snapshot.local = local;
        draw_view(state, &snapshot, screen, selected);
        key = getch();
        if (key == 'q' || key == 'Q') break;
        if (key >= '1' && key <= '7') {
            screen = key - '0';
            selected = 0;
        } else if (key == KEY_UP || key == 'k') {
            if (selected > 0) selected--;
        } else if (key == KEY_DOWN || key == 'j') {
            size_t count = screen == 2 ? snapshot.server_count :
                           screen == 3 ? snapshot.service_count :
                           (screen == 4 || screen == 5) ? snapshot.device_count : 0;
            if ((size_t)(selected + 1) < count) selected++;
        } else if (key == 'r' || key == 'R') {
            request_scan(state);
        } else if (key == 'w' || key == 'W') {
            wake_server(state, &snapshot, screen, selected);
        }
    }
    endwin();
    return 0;
}