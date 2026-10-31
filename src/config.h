#ifndef UPTIME_CONFIG_H
#define UPTIME_CONFIG_H

#include "app.h"

void config_defaults(AppConfig *config);
int config_load(const char *path, AppConfig *config, char *error, size_t error_size);
int config_validate_subnet(const char *subnet);
int config_write_example(const char *path);

#endif