#include "config.h"
#include "db.h"
#include "ui.h"
#include "worker.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define UPTIME_VERSION "0.1.0"

int main(int argc, char **argv)
{
    AppState state;
    pthread_t worker;
    char error[UPTIME_DETAIL];
    const char *config_path = NULL;

    if (argc == 2 && (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-V") == 0)) {
        printf("Uptime %s\n", UPTIME_VERSION);
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "--init-config") == 0) {
        if (config_write_example(argv[2]) != 0) {
            perror(argv[2]);
            return 1;
        }
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "--check-config") == 0) {
        AppConfig config;
        if (config_load(argv[2], &config, error, sizeof(error)) != 0) {
            fprintf(stderr, "%s\n", error);
            return 1;
        }
        printf("Configuration valid: %zu server(s), %zu service(s), subnet %s\n",
               config.server_count, config.service_count, config.subnet);
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "--init-db") == 0) {
        sqlite3 *database = NULL;
        if (db_open(&database, argv[2], error, sizeof(error)) != 0) {
            fprintf(stderr, "%s\n", error);
            return 1;
        }
        sqlite3_close(database);
        printf("Database initialized: %s\n", argv[2]);
        return 0;
    }
    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config") == 0) && i + 1 < argc) {
            config_path = argv[++i];
        } else {
            fprintf(stderr, "Usage: %s [-c FILE] | --version | --check-config FILE | --init-config FILE | --init-db FILE\n", argv[0]);
            return 2;
        }
    }

    memset(&state, 0, sizeof(state));
    config_defaults(&state.config);
    if (config_path == NULL) {
        if (access("./uptime.conf", R_OK) == 0) config_path = "./uptime.conf";
        else if (access("/etc/uptime/uptime.conf", R_OK) == 0) config_path = "/etc/uptime/uptime.conf";
    }
    if (config_path != NULL) {
        if (config_load(config_path, &state.config, error, sizeof(error)) != 0) {
            fprintf(stderr, "%s\n", error);
            return 1;
        }
        snprintf(state.config_file, sizeof(state.config_file), "%s", config_path);
    } else {
        snprintf(state.snapshot.notice, sizeof(state.snapshot.notice), "No config loaded; local-only mode. Run with -c FILE to monitor configured hosts.");
    }
    {
        int thread_error = pthread_mutex_init(&state.mutex, NULL);
        if (thread_error != 0) {
            fprintf(stderr, "cannot initialize mutex: %s\n", strerror(thread_error));
            return 1;
        }
        thread_error = pthread_cond_init(&state.condition, NULL);
        if (thread_error != 0) {
            fprintf(stderr, "cannot initialize condition variable: %s\n", strerror(thread_error));
            pthread_mutex_destroy(&state.mutex);
            return 1;
        }
    }
    state.snapshot.server_count = state.config.server_count;
    state.snapshot.service_count = state.config.service_count;
    for (size_t i = 0; i < state.config.server_count; i++) {
        ServerStatus *status = &state.snapshot.servers[i];
        snprintf(status->name, sizeof(status->name), "%s", state.config.servers[i].name);
        snprintf(status->address, sizeof(status->address), "%s", state.config.servers[i].address);
        status->cpu_percent = -1.0;
        status->memory_percent = -1.0;
        status->disk_percent = -1.0;
    }
    for (size_t i = 0; i < state.config.service_count; i++) {
        ServiceStatus *status = &state.snapshot.services[i];
        snprintf(status->name, sizeof(status->name), "%s", state.config.services[i].name);
        snprintf(status->server, sizeof(status->server), "%s", state.config.services[i].server);
        snprintf(status->target, sizeof(status->target), "%s", state.config.services[i].target);
    }
    {
        int database_result = db_open(&state.database, state.config.database, error, sizeof(error));
        if (database_result != 0) {
            fprintf(stderr, "cannot initialize database: %s\n", error);
            pthread_cond_destroy(&state.condition);
            pthread_mutex_destroy(&state.mutex);
            return 1;
        }
    }
    {
        int thread_error = pthread_create(&worker, NULL, worker_main, &state);
        if (thread_error != 0) {
            fprintf(stderr, "cannot start monitoring worker: %s\n", strerror(thread_error));
            sqlite3_close(state.database);
            pthread_cond_destroy(&state.condition);
            pthread_mutex_destroy(&state.mutex);
            return 1;
        }
    }
    ui_run(&state);
    worker_stop(&state, worker);
    sqlite3_close(state.database);
    pthread_cond_destroy(&state.condition);
    pthread_mutex_destroy(&state.mutex);
    return 0;
}