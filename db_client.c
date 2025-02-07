#include "db_client.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Add response structure for curl callback
typedef struct {
    uint8_t* buffer;
    size_t size;
} CurlResponse;

// Curl callback function
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total_size = size * nmemb;
    CurlResponse* resp = (CurlResponse*)userp;
    
    if (resp->size + total_size > 1024) {  // Prevent buffer overflow
        return 0;  // Return 0 to indicate error
    }
    
    memcpy(resp->buffer + resp->size, contents, total_size);
    resp->size += total_size;
    return total_size;
}

// Add header debug callback
static size_t HeaderCallback(char* buffer, size_t size, size_t nitems, void* userdata) {
    size_t totalSize = size * nitems;
    fprintf(stderr, "Received header: %.*s", (int)totalSize, buffer);
    return totalSize;
}

bool initDatabaseClient(DatabaseClient* client, const char* host, int port,
                       const char* server_id, const char* server_token) {
    client->host = strdup(host);
    client->port = port;
    client->server_id = strdup(server_id);
    client->server_token = strdup(server_token);
    client->curl = curl_easy_init();
    
    if (!client->curl || !client->server_id || !client->server_token) {
        return false;
    }
    
    curl_easy_setopt(client->curl, CURLOPT_ERRORBUFFER, client->error_buffer);
    curl_easy_setopt(client->curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    
    return true;
}

bool checkDatabaseHealth(DatabaseClient* client, DatabaseHealth* health) {
    uint8_t request[HEADER_SIZE] = {MSG_HEALTH_CHECK, 0, 0, 0, 0};  // 0x04 and length=0
    uint8_t response_buffer[1024] = {0};
    
    CurlResponse response = {
        .buffer = response_buffer,
        .size = 0
    };
    
    char url[256];
    snprintf(url, sizeof(url), "http://%s:%d/health", client->host, client->port);
    
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/octet-stream");
    headers = curl_slist_append(headers, "Accept: application/octet-stream");
    
    // Add required game server headers
    char auth_header1[512], auth_header2[512], host_header[256];
    snprintf(auth_header1, sizeof(auth_header1), "X-Game-Server-ID: %s", client->server_id);
    snprintf(auth_header2, sizeof(auth_header2), "X-Game-Server-Token: %s", client->server_token);
    snprintf(host_header, sizeof(host_header), "Host: %s:%d", client->host, client->port);
    
    headers = curl_slist_append(headers, auth_header1);
    headers = curl_slist_append(headers, auth_header2);
    headers = curl_slist_append(headers, host_header);
    
    // Set up GET request with binary body
    curl_easy_setopt(client->curl, CURLOPT_URL, url);
    curl_easy_setopt(client->curl, CURLOPT_HTTPGET, 1L);           // Use GET method
    curl_easy_setopt(client->curl, CURLOPT_POSTFIELDS, request);   // Binary data
    curl_easy_setopt(client->curl, CURLOPT_POSTFIELDSIZE, HEADER_SIZE);
    curl_easy_setopt(client->curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(client->curl, CURLOPT_WRITEDATA, &response);
    
    // Debug settings
    curl_easy_setopt(client->curl, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt(client->curl, CURLOPT_HEADERFUNCTION, HeaderCallback);
    curl_easy_setopt(client->curl, CURLOPT_HEADERDATA, NULL);
    
    CURLcode res = curl_easy_perform(client->curl);
    curl_slist_free_all(headers);
    
    if (res != CURLE_OK) {
        fprintf(stderr, "Curl error: %s\n", curl_easy_strerror(res));
        return false;
    }

    // Verify response format
    if (response.size < 42 || response_buffer[0] != MSG_HEALTH_RESP) {
        fprintf(stderr, "Invalid response format: type=0x%02x, size=%zu\n", 
                response_buffer[0], response.size);
        return false;
    }

    // Parse fixed-size binary response
    health->status = response_buffer[5];
    health->timestamp = readUint64(response_buffer + 6);
    health->db_latency = readUint32(response_buffer + 14);
    health->memory_used = readUint64(response_buffer + 18);
    health->memory_total = readUint64(response_buffer + 26);
    health->uptime_ms = readUint64(response_buffer + 34);
    
    return true;
}

bool verifyUserToken(DatabaseClient* client, const char* token, TokenVerifyResult* result) {
    // Same header setup as above for consistency
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/octet-stream");
    headers = curl_slist_append(headers, "Accept: application/octet-stream");
    
    char auth_header1[512], auth_header2[512];
    snprintf(auth_header1, sizeof(auth_header1), "X-Game-Server-ID: %s", client->server_id);
    snprintf(auth_header2, sizeof(auth_header2), "X-Game-Server-Token: %s", client->server_token);
    headers = curl_slist_append(headers, auth_header1);
    headers = curl_slist_append(headers, auth_header2);
    headers = curl_slist_append(headers, "Host: localhost:3000");
    
    // ...rest of verification code...
}

void cleanupDatabaseClient(DatabaseClient* client) {
    if (client->curl) {
        curl_easy_cleanup(client->curl);
    }
    free(client->host);
    free(client->server_id);     // Free server ID
    free(client->server_token);  // Free server token
}
