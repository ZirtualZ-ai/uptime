#include "ui.h"

#include "wol.h"
#include "db.h"

#include <ncurses.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define UPTIME_VERSION "0.2.0"

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
    const char *shortcuts = " 1-7 Views   Up/Down Select   Enter/S SSH   W Wake   R Scan   Q Quit ";
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
    put_text(3, 2, "MANAGED SERVERS  (Enter/S SSH  W wake)", columns - 4, 1, 1);
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

static int device_is_managed(const AppState *state, const DeviceRecord *device)
{
    if (device->managed) return 1;
    for (size_t i = 0; i < state->config.server_count; i++) {
        const ServerConfig *server = &state->config.servers[i];
        size_t name_length = strlen(server->name);
        const char *dot = strchr(device->hostname, '.');

        if (strcmp(server->address, device->ip) == 0) return 1;
        if (device->hostname[0] != '\0' &&
            (strcasecmp(server->name, device->hostname) == 0 ||
             (dot != NULL && (size_t)(dot - device->hostname) == name_length &&
              strncasecmp(server->name, device->hostname, name_length) == 0))) return 1;
    }
    return 0;
}

static void draw_devices(const AppState *state, const AppSnapshot *snapshot, int columns, int history, int selected)
{
    int visible = LINES - 9;
    int first = selected >= visible ? selected - visible + 1 : 0;
    char row[UPTIME_DETAIL];
    put_text(3, 2, history ? "DEVICE HISTORY" : "NETWORK RECONNAISSANCE", columns - 4, 1, 1);
    if (!history) {
        snprintf(row, sizeof(row), "Subnet %.40s  Profile %s  %.120s",
                 state->config.subnet,
                 state->config.scan_profile == SCAN_PROFILE_STANDARD ? "STANDARD" : "DISCOVERY",
                 snapshot->scanning ? snapshot->scan_message : "READY");
        put_text(4, 3, row, columns - 6, snapshot->scanning ? 3 : 0, 0);
    } else {
        put_text(4, 3, "Retained device history", columns - 6, 3, 0);
    }
    if (!history) put_text(5, 3, "Enter connect  S enroll  P profile  R scan  X stop", columns - 6, 3, 0);
    put_text(6, 3, "STATE  IP              HOSTNAME               MAC                VENDOR           SSH      PORTS", columns - 6, 5, 1);
    for (int i = first; i < (int)snapshot->device_count && i < first + visible; i++) {
        const DeviceRecord *device = &snapshot->devices[i];
        const char *ssh_state = device->managed || device->ssh_open ? "OPEN" :
            device->ports_known ? "CLOSED" : "?";
        snprintf(row, sizeof(row), "%-5s  %-15s %-22s %-18s %-16.16s %-8s %-40.40s %s", device->online ? "UP" : "DOWN",
                 device->ip, device->hostname[0] ? device->hostname : "-",
             device->mac[0] ? device->mac : "-", device->vendor[0] ? device->vendor : "-", ssh_state,
                 device->open_ports[0] ? device->open_ports : "-",
                 device_is_managed(state, device) ? "MANAGED + SSH" : "DISCOVERED");
        put_text(7 + i - first, 3, row, columns - 6, device->online ? 2 : (history ? 5 : 4), i == selected);
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
    if (snapshot->device_count == 0) put_text(7, 3, "No device history yet.", columns - 6, 5, 0);
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
            snprintf(text, sizeof(text), "%c %s %.18s %.16s SSH:%s %s",
                     (int)i == selected ? '>' : ' ', device->online ? "UP" : "--",
                     device->ip, device->hostname[0] ? device->hostname : device->mac,
                     device->managed || device->ssh_open ? "YES" : "NO",
                     device_is_managed(state, device) ? "MANAGED+SSH" : "DISCOVERED");
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
        snprintf(text, sizeof(text), "1-7 views  Enter/S connect  R scan  P profile  X stop  W wake  Q quit");
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
    case 4: draw_devices(state, snapshot, columns, 0, selected); break;
    case 5: draw_devices(state, snapshot, columns, 1, selected); break;
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
    if (state->snapshot.scanning) {
        atomic_store(&state->scan_cancelled, 1);
        snprintf(state->snapshot.notice, sizeof(state->snapshot.notice), "Stopping the active network scan");
    } else {
        atomic_store(&state->scan_cancelled, 0);
        state->scan_requested = 1;
        pthread_cond_signal(&state->condition);
        snprintf(state->snapshot.notice, sizeof(state->snapshot.notice), "Network scan queued for %s", state->config.subnet);
    }
    pthread_mutex_unlock(&state->mutex);
}

static void toggle_scan_profile(AppState *state)
{
    pthread_mutex_lock(&state->mutex);
    state->config.scan_profile = state->config.scan_profile == SCAN_PROFILE_DISCOVERY ?
        SCAN_PROFILE_STANDARD : SCAN_PROFILE_DISCOVERY;
    snprintf(state->snapshot.notice, sizeof(state->snapshot.notice), "Scan profile: %s",
             state->config.scan_profile == SCAN_PROFILE_STANDARD ? "STANDARD (bounded common TCP ports)" : "DISCOVERY");
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
        if (snapshot->servers[selected].wol_mac[0] == '\0') {
            snprintf(notice, sizeof(notice), "%s has no wol_mac configured", server->name);
        } else if (wol_send(snapshot->servers[selected].wol_mac, broadcast) != 0) {
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

static void connect_server(AppState *state, int screen, int selected)
{
    const ServerConfig *server;
    char port[16];
    pid_t child;
    int status;
    char notice[UPTIME_DETAIL];

    if (screen != 2 || selected < 0 || (size_t)selected >= state->config.server_count) {
        snprintf(notice, sizeof(notice), "Select a managed server from the Servers view first");
        goto report;
    }
    server = &state->config.servers[selected];
    snprintf(port, sizeof(port), "%d", server->port);
    snprintf(notice, sizeof(notice), "SSH session ended for %s", server->name);
    def_prog_mode();
    endwin();
    fflush(NULL);
    child = fork();
    if (child == 0) {
        execlp("ssh", "ssh", "-t", "-p", port, "-l", server->user, server->address, (char *)NULL);
        perror("uptime-console: ssh");
        _exit(127);
    }
    if (child < 0) {
        snprintf(notice, sizeof(notice), "Cannot start SSH for %s", server->name);
    } else {
        while (waitpid(child, &status, 0) < 0) {
            if (errno == EINTR) continue;
            snprintf(notice, sizeof(notice), "Error waiting for SSH to %s", server->name);
            break;
        }
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            db_mark_ssh_success(state->database, server->address);
        }
    }
    reset_prog_mode();
    clearok(stdscr, TRUE);
    refresh();

report:
    pthread_mutex_lock(&state->mutex);
    snprintf(state->snapshot.notice, sizeof(state->snapshot.notice), "%s", notice);
    pthread_mutex_unlock(&state->mutex);
}

static int prompt_text(const char *label, char *value, size_t value_size)
{
    int row = LINES > 2 ? LINES - 2 : LINES - 1;
    int result;

    echo();
    curs_set(1);
    timeout(-1);
    move(row, 0);
    clrtoeol();
    addnstr(label, COLS - 1);
    refresh();
    result = getnstr(value, (int)value_size - 1);
    noecho();
    curs_set(0);
    timeout(1000);
    return result != ERR;
}

static int ssh_log_authenticated(const char *path)
{
    FILE *file = fopen(path, "r");
    char line[512];
    int authenticated = 0;
    if (file == NULL) return 0;
    while (fgets(line, sizeof(line), file) != NULL) {
        if (strstr(line, "Authenticated to ") != NULL) {
            authenticated = 1;
            break;
        }
    }
    fclose(file);
    return authenticated;
}

static int ssh_device(AppState *state, const DeviceRecord *selected_device, int prompt_identity)
{
    DeviceRecord device = *selected_device;
    char port_text[16];
    char prompt[UPTIME_DETAIL];
    char message[UPTIME_DETAIL];
    char auth_log[] = "/tmp/uptime-console-ssh-XXXXXX";
    char *end;
    long port;
    pid_t child;
    int status = -1;
    int auth_log_fd = -1;

    if (device.managed && !prompt_identity) {
        snprintf(device.ssh_user, sizeof(device.ssh_user), "%s", selected_device->ssh_user);
        device.ssh_port = selected_device->ssh_port;
    } else {
        snprintf(prompt, sizeof(prompt), "SSH username for %s: ", device.hostname[0] ? device.hostname : device.ip);
        if (!prompt_text(prompt, device.ssh_user, sizeof(device.ssh_user)) || device.ssh_user[0] == '\0') return -1;
        snprintf(port_text, sizeof(port_text), "%d", device.ssh_port > 0 ? device.ssh_port : 22);
        snprintf(prompt, sizeof(prompt), "SSH port [%s]: ", port_text);
        {
            char entered[16] = "";
            if (!prompt_text(prompt, entered, sizeof(entered))) return -1;
            if (entered[0] != '\0') snprintf(port_text, sizeof(port_text), "%s", entered);
        }
        errno = 0;
        port = strtol(port_text, &end, 10);
        if (errno != 0 || *end != '\0' || port < 1 || port > 65535) {
            snprintf(message, sizeof(message), "Invalid SSH port: %.12s", port_text);
            goto report;
        }
        device.ssh_port = (int)port;
    }
    snprintf(port_text, sizeof(port_text), "%d", device.ssh_port);
    auth_log_fd = mkstemp(auth_log);
    if (auth_log_fd >= 0) close(auth_log_fd);
    def_prog_mode();
    endwin();
    fflush(NULL);
    child = fork();
    if (child == 0) {
        if (auth_log_fd >= 0) close(auth_log_fd);
        if (auth_log_fd >= 0) {
            execlp("ssh", "ssh", "-vv", "-E", auth_log, "-t", "-p", port_text,
                   "-l", device.ssh_user, device.ip, (char *)NULL);
        } else {
            execlp("ssh", "ssh", "-t", "-p", port_text, "-l", device.ssh_user, device.ip, (char *)NULL);
        }
        _exit(127);
    }
    if (child < 0) {
        snprintf(message, sizeof(message), "Cannot start SSH for %s", device.ip);
    } else {
        do {
            if (waitpid(child, &status, 0) >= 0) break;
        } while (errno == EINTR);
        if ((!WIFEXITED(status) || WEXITSTATUS(status) != 0) && !ssh_log_authenticated(auth_log)) {
            snprintf(message, sizeof(message), "SSH did not complete successfully for %s; not enrolled", device.ip);
            status = -1;
        } else {
            device.managed = 1;
            device.ssh_open = 1;
            device.online = 1;
            device.last_ssh_success = time(NULL);
            device.last_seen = device.last_ssh_success;
            if (device.mac[0] == '\0') local_neighbor_mac(device.ip, device.mac, sizeof(device.mac));
            if (db_enroll_device(state->database, &device) == 0) {
                snprintf(message, sizeof(message), "SSH succeeded; %s enrolled as %s on port %d",
                         device.ip, device.ssh_user, device.ssh_port);
                status = 0;
                pthread_mutex_lock(&state->mutex);
                {
                    size_t server_index;
                    for (server_index = 0; server_index < state->config.server_count; server_index++) {
                        if (strcmp(state->config.servers[server_index].address, device.ip) == 0) break;
                    }
                    if (server_index == state->config.server_count && server_index < UPTIME_MAX_SERVERS) {
                        ServerConfig *server = &state->config.servers[server_index];
                        ServerStatus *server_status = &state->snapshot.servers[server_index];
                        memset(server, 0, sizeof(*server));
                        snprintf(server->name, sizeof(server->name), "%s", device.hostname[0] ? device.hostname : device.ip);
                        snprintf(server->address, sizeof(server->address), "%s", device.ip);
                        snprintf(server->user, sizeof(server->user), "%s", device.ssh_user);
                        snprintf(server->wol_mac, sizeof(server->wol_mac), "%s", device.mac);
                        server->port = device.ssh_port;
                        memset(server_status, 0, sizeof(*server_status));
                        snprintf(server_status->name, sizeof(server_status->name), "%s",
                                 device.hostname[0] ? device.hostname : device.ip);
                        snprintf(server_status->address, sizeof(server_status->address), "%s", device.ip);
                        snprintf(server_status->wol_mac, sizeof(server_status->wol_mac), "%s", device.mac);
                        server_status->cpu_percent = -1.0;
                        server_status->memory_percent = -1.0;
                        server_status->disk_percent = -1.0;
                        state->config.server_count++;
                        state->snapshot.server_count = state->config.server_count;
                    } else if (server_index < UPTIME_MAX_SERVERS) {
                        snprintf(state->config.servers[server_index].user, sizeof(state->config.servers[server_index].user), "%s", device.ssh_user);
                        state->config.servers[server_index].port = device.ssh_port;
                    }
                    for (size_t i = 0; i < state->snapshot.device_count; i++) {
                        if (strcmp(state->snapshot.devices[i].ip, device.ip) == 0) {
                            state->snapshot.devices[i] = device;
                            break;
                        }
                    }
                }
                pthread_mutex_unlock(&state->mutex);
                db_add_event(state->database, 0, message);
            } else {
                snprintf(message, sizeof(message), "SSH succeeded, but enrollment could not be saved for %s", device.ip);
                status = -1;
            }
        }
    }
    if (auth_log_fd >= 0) unlink(auth_log);
    reset_prog_mode();
    clearok(stdscr, TRUE);
    refresh();

report:
    pthread_mutex_lock(&state->mutex);
    snprintf(state->snapshot.notice, sizeof(state->snapshot.notice), "%s", message);
    pthread_mutex_unlock(&state->mutex);
    return status;
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
        } else if (screen == 4 && (key == 'p' || key == 'P')) {
            toggle_scan_profile(state);
        } else if (screen == 4 && key == 'x' && snapshot.scanning) {
            request_scan(state);
        } else if (key == 'w' || key == 'W') {
            wake_server(state, &snapshot, screen, selected);
        } else if (screen == 2 && (key == '\n' || key == '\r' || key == KEY_ENTER || key == 's' || key == 'S')) {
            connect_server(state, screen, selected);
        } else if (screen == 4 && (key == '\n' || key == '\r' || key == KEY_ENTER || key == 's' || key == 'S') &&
                   selected >= 0 && (size_t)selected < snapshot.device_count) {
            DeviceRecord *device = &snapshot.devices[selected];
            int enroll_prompt = key == 's' || key == 'S';
            if (!device->managed && !device->ssh_open && !enroll_prompt) {
                pthread_mutex_lock(&state->mutex);
                snprintf(state->snapshot.notice, sizeof(state->snapshot.notice), "SSH not identified; use S to enter a username and port");
                pthread_mutex_unlock(&state->mutex);
            } else {
                ssh_device(state, device, enroll_prompt);
            }
        }
    }
    endwin();
    return 0;
}