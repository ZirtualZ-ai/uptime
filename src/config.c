#include "config.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void copy_text(char *destination, size_t size, const char *source)
{
    snprintf(destination, size, "%s", source);
}

static char *trim(char *text)
{
    char *end;

    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') {
        text++;
    }
    end = text + strlen(text);
    while (end > text && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) {
        *--end = '\0';
    }
    return text;
}

static int parse_uint(const char *text, unsigned int minimum, unsigned int maximum, unsigned int *value)
{
    char *end;
    unsigned long parsed;

    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (errno != 0 || *text == '\0' || *end != '\0' || parsed < minimum || parsed > maximum) {
        return -1;
    }
    *value = (unsigned int)parsed;
    return 0;
}

int config_validate_subnet(const char *subnet)
{
    char copy[64];
    char *slash;
    unsigned int prefix;
    struct in_addr address;

    if (strlen(subnet) >= sizeof(copy)) {
        return -1;
    }
    copy_text(copy, sizeof(copy), subnet);
    slash = strchr(copy, '/');
    if (slash == NULL) {
        return -1;
    }
    *slash++ = '\0';
    return inet_pton(AF_INET, copy, &address) == 1 && parse_uint(slash, 0, 32, &prefix) == 0 ? 0 : -1;
}

void config_defaults(AppConfig *config)
{
    memset(config, 0, sizeof(*config));
    copy_text(config->subnet, sizeof(config->subnet), "192.168.40.0/24");
    copy_text(config->database, sizeof(config->database), "./uptime.db");
    config->scan_interval = 300;
    config->monitor_interval = 60;
    config->scan_enabled = 0;
}

static int set_global(AppConfig *config, const char *key, const char *value)
{
    unsigned int parsed;

    if (strcmp(key, "subnet") == 0) {
        if (config_validate_subnet(value) != 0) return -1;
        copy_text(config->subnet, sizeof(config->subnet), value);
    } else if (strcmp(key, "database") == 0) {
        copy_text(config->database, sizeof(config->database), value);
    } else if (strcmp(key, "scan_interval") == 0) {
        if (parse_uint(value, 30, 86400, &parsed) != 0) return -1;
        config->scan_interval = parsed;
    } else if (strcmp(key, "monitor_interval") == 0) {
        if (parse_uint(value, 10, 3600, &parsed) != 0) return -1;
        config->monitor_interval = parsed;
    } else if (strcmp(key, "scan_enabled") == 0) {
        if (strcmp(value, "true") == 0 || strcmp(value, "yes") == 0 || strcmp(value, "1") == 0) {
            config->scan_enabled = 1;
        } else if (strcmp(value, "false") == 0 || strcmp(value, "no") == 0 || strcmp(value, "0") == 0) {
            config->scan_enabled = 0;
        } else return -1;
    } else {
        return -1;
    }
    return 0;
}

static int set_server(ServerConfig *server, const char *key, const char *value)
{
    unsigned int parsed;

    if (strcmp(key, "address") == 0) copy_text(server->address, sizeof(server->address), value);
    else if (strcmp(key, "user") == 0) copy_text(server->user, sizeof(server->user), value);
    else if (strcmp(key, "port") == 0) {
        if (parse_uint(value, 1, 65535, &parsed) != 0) return -1;
        server->port = (int)parsed;
    } else if (strcmp(key, "wol_mac") == 0) copy_text(server->wol_mac, sizeof(server->wol_mac), value);
    else if (strcmp(key, "broadcast") == 0) copy_text(server->broadcast, sizeof(server->broadcast), value);
    else return -1;
    return 0;
}

static int set_service(ServiceConfig *service, const char *key, const char *value)
{
    unsigned int parsed;

    if (strcmp(key, "server") == 0) copy_text(service->server, sizeof(service->server), value);
    else if (strcmp(key, "type") == 0) {
        if (strcmp(value, "tcp") != 0 && strcmp(value, "http") != 0 && strcmp(value, "https") != 0) return -1;
        copy_text(service->type, sizeof(service->type), value);
    } else if (strcmp(key, "target") == 0) copy_text(service->target, sizeof(service->target), value);
    else if (strcmp(key, "port") == 0) {
        if (parse_uint(value, 1, 65535, &parsed) != 0) return -1;
        service->port = (int)parsed;
    } else if (strcmp(key, "expected_status") == 0) {
        if (parse_uint(value, 0, 599, &parsed) != 0 || (parsed > 0 && parsed < 100)) return -1;
        service->expected_status = (int)parsed;
    } else if (strcmp(key, "interval") == 0) {
        if (parse_uint(value, 5, 3600, &parsed) != 0) return -1;
        service->interval = parsed;
    } else return -1;
    return 0;
}

int config_load(const char *path, AppConfig *config, char *error, size_t error_size)
{
    FILE *file;
    char line[1024];
    char section[UPTIME_NAME] = "global";
    enum { SECTION_GLOBAL, SECTION_SERVER, SECTION_SERVICE } section_type = SECTION_GLOBAL;
    ServerConfig *server = NULL;
    ServiceConfig *service = NULL;
    unsigned int line_number = 0;

    config_defaults(config);
    file = fopen(path, "r");
    if (file == NULL) {
        snprintf(error, error_size, "cannot open %s: %s", path, strerror(errno));
        return -1;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        char *text;
        line_number++;
        text = trim(line);
        if (*text == '\0' || *text == '#' || *text == ';') continue;
        if (*text == '[') {
            char *end = strrchr(text, ']');
            char *kind;
            char *name;
            if (end == NULL || end[1] != '\0') goto invalid_line;
            *end = '\0';
            kind = trim(text + 1);
            if (strcmp(kind, "global") == 0) {
                copy_text(section, sizeof(section), "global");
                section_type = SECTION_GLOBAL;
                server = NULL;
                service = NULL;
                continue;
            }
            name = strchr(kind, ' ');
            if (name == NULL) goto invalid_line;
            *name++ = '\0';
            name = trim(name);
            if (*name == '\0' || strlen(name) >= sizeof(section)) goto invalid_line;
            copy_text(section, sizeof(section), name);
            if (strcmp(kind, "server") == 0) {
                if (config->server_count >= UPTIME_MAX_SERVERS) goto invalid_line;
                server = &config->servers[config->server_count++];
                memset(server, 0, sizeof(*server));
                server->port = 22;
                copy_text(server->name, sizeof(server->name), section);
                section_type = SECTION_SERVER;
                service = NULL;
            } else if (strcmp(kind, "service") == 0) {
                if (config->service_count >= UPTIME_MAX_SERVICES) goto invalid_line;
                service = &config->services[config->service_count++];
                memset(service, 0, sizeof(*service));
                service->interval = config->monitor_interval;
                service->expected_status = 200;
                copy_text(service->name, sizeof(service->name), section);
                section_type = SECTION_SERVICE;
                server = NULL;
            } else goto invalid_line;
            continue;
        }
        {
            char *equals = strchr(text, '=');
            char *key;
            char *value;
            int result;
            if (equals == NULL) goto invalid_line;
            *equals = '\0';
            key = trim(text);
            value = trim(equals + 1);
            if (*key == '\0' || *value == '\0') goto invalid_line;
            if (section_type == SECTION_GLOBAL) result = set_global(config, key, value);
            else if (section_type == SECTION_SERVER && server != NULL) result = set_server(server, key, value);
            else if (section_type == SECTION_SERVICE && service != NULL) result = set_service(service, key, value);
            else result = -1;
            if (result != 0) goto invalid_line;
        }
    }
    if (ferror(file)) {
        snprintf(error, error_size, "error reading %s", path);
        fclose(file);
        return -1;
    }
    fclose(file);
    for (size_t i = 0; i < config->server_count; i++) {
        if (config->servers[i].address[0] == '\0' || config->servers[i].user[0] == '\0') {
            snprintf(error, error_size, "server '%s' needs address and user", config->servers[i].name);
            return -1;
        }
    }
    for (size_t i = 0; i < config->service_count; i++) {
        ServiceConfig *item = &config->services[i];
        if (item->type[0] == '\0' || item->target[0] == '\0' || item->port == 0) {
            snprintf(error, error_size, "service '%s' needs type, target, and port", item->name);
            return -1;
        }
    }
    if (error_size > 0) error[0] = '\0';
    return 0;

invalid_line:
    snprintf(error, error_size, "invalid setting or section at %s:%u", path, line_number);
    fclose(file);
    return -1;
}

int config_write_example(const char *path)
{
    FILE *file = fopen(path, "w");
    if (file == NULL) return -1;
    fputs("[global]\nsubnet = 192.168.40.0/24\nscan_interval = 300\nmonitor_interval = 60\ndatabase = ./uptime.db\nscan_enabled = true\n\n"
          "[server b460m]\naddress = 192.168.40.10\nuser = marshal\nport = 22\n\n"
          "[service website]\nserver = b460m\ntype = https\ntarget = https://zirtualz.net/\nport = 443\nexpected_status = 200\ninterval = 60\n\n"
          "[service ollama]\nserver = b460m\ntype = tcp\ntarget = 192.168.40.10\nport = 11434\ninterval = 30\n\n"
          "[service open-webui]\nserver = b460m\ntype = tcp\ntarget = 192.168.40.10\nport = 3000\ninterval = 30\n\n"
          "[service ai-gateway]\nserver = b460m\ntype = tcp\ntarget = 192.168.40.10\nport = 7681\ninterval = 30\n\n"
          "[service minecraft]\nserver = b460m\ntype = tcp\ntarget = 192.168.40.10\nport = 25565\ninterval = 30\n\n"
          "[service ssh]\nserver = b460m\ntype = tcp\ntarget = 192.168.40.10\nport = 22\ninterval = 30\n", file);
    return fclose(file);
}