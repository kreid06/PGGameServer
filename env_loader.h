#ifndef ENV_LOADER_H
#define ENV_LOADER_H

#include <stdbool.h>

bool loadEnvFile(const char* filename);
const char* getEnvOrDefault(const char* key, const char* default_value);

#endif // ENV_LOADER_H
