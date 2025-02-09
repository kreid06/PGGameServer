#include "env_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>

#define MAX_LINE_LENGTH 4096
#define MAX_KEY_LENGTH 256
#define MAX_VALUE_LENGTH 4096  // Increased from 3072 to handle long tokens

static void normalizeValue(char* value) {
    // Remove line breaks and spaces from value
    char* read = value;
    char* write = value;
    
    while (*read) {
        if (*read != '\n' && *read != '\r') {
            *write = *read;
            write++;
        }
        read++;
    }
    *write = '\0';
}

bool loadEnvFile(const char* filename) {
    if (!filename) {
        fprintf(stderr, "Error: NULL filename provided\n");
        return false;
    }

    FILE* file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "Error opening %s: %s (cwd: ", filename, strerror(errno));
        // Print current working directory for debugging
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd))) {
            fprintf(stderr, "%s)\n", cwd);
        } else {
            fprintf(stderr, "unknown)\n");
        }
        return false;
    }

    char line[MAX_LINE_LENGTH];
    char key[MAX_KEY_LENGTH];
    char value[MAX_VALUE_LENGTH];

    while (fgets(line, sizeof(line), file)) {
        // Check for buffer overflow
        if (strlen(line) >= MAX_LINE_LENGTH - 2) {
            fprintf(stderr, "Error: Line too long in .env file\n");
            fclose(file);
            return false;
        }

        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\0') {
            continue;
        }

        // Remove newline and carriage return
        line[strcspn(line, "\r\n")] = 0;
        
        // Find first equals sign
        char* first_equals = strchr(line, '=');
        if (first_equals) {
            // Split into key and value at first equals sign only
            size_t key_len = first_equals - line;
            if (key_len >= MAX_KEY_LENGTH) {
                fprintf(stderr, "Error: Key too long in .env file\n");
                fclose(file);
                return false;
            }

            // Copy and trim key
            memcpy(key, line, key_len);
            key[key_len] = '\0';
            
            // Trim trailing whitespace from key
            while (key_len > 0 && isspace(key[key_len - 1])) {
                key[--key_len] = '\0';
            }

            // Value starts after the first equals sign
            const char* value_start = first_equals + 1;
            while (isspace(*value_start)) value_start++;
            
            // Copy entire remaining line as value (preserving any additional equals signs)
            size_t value_len = strlen(value_start);
            if (value_len >= MAX_VALUE_LENGTH) {
                fprintf(stderr, "Error: Value too long in .env file\n");
                fclose(file);
                return false;
            }

            // Remove trailing whitespace and newlines from value
            strncpy(value, value_start, MAX_VALUE_LENGTH - 1);
            value[MAX_VALUE_LENGTH - 1] = '\0';
            
            // Normalize the value by removing line breaks
            normalizeValue(value);
            
            // Only set if both key and value are non-empty
            if (*key && *value) {
                if (setenv(key, value, 1) != 0) {
                    fprintf(stderr, "Error setting environment variable %s\n", key);
                    fclose(file);
                    return false;
                }
                fprintf(stderr, "DEBUG: Set env var %s with length %zu\n", 
                        key, strlen(value));
            }
        }
    }

    fclose(file);
    return true;
}

const char* getEnvOrDefault(const char* key, const char* default_value) {
    const char* value = getenv(key);
    return value ? value : default_value;
}
