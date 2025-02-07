#include "net_protocol.h"
#include <string.h>
#include <arpa/inet.h>

uint32_t readUint32(const uint8_t* buffer) {
    uint32_t value;
    memcpy(&value, buffer, sizeof(value));
    return ntohl(value);
}

uint64_t readUint64(const uint8_t* buffer) {
    uint64_t value;
    memcpy(&value, buffer, sizeof(value));
    return be64toh(value);
}

void writeUint32(uint8_t* buffer, uint32_t value) {
    uint32_t netValue = htonl(value);
    memcpy(buffer, &netValue, sizeof(netValue));
}

void writeUint64(uint8_t* buffer, uint64_t value) {
    uint64_t netValue = htobe64(value);
    memcpy(buffer, &netValue, sizeof(netValue));
}
