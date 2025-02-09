#include "net_protocol.h"
#include <string.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>

uint32_t readUint32(const uint8_t* buffer) {
    // Read bytes in correct order (little-endian)
    uint32_t value = ((uint32_t)buffer[3] << 24) |
                     ((uint32_t)buffer[2] << 16) |
                     ((uint32_t)buffer[1] << 8)  |
                     ((uint32_t)buffer[0]);

    #ifdef DEBUG
    fprintf(stderr, "Reading uint32 bytes: %02x %02x %02x %02x = %u\n",
            buffer[0], buffer[1], buffer[2], buffer[3], value);
    #endif

    return value;
}

uint64_t readUint64(const uint8_t* buffer) {
    // Convert bytes from little-endian to host byte order
    uint64_t value = ((uint64_t)buffer[7] << 56) |
                     ((uint64_t)buffer[6] << 48) |
                     ((uint64_t)buffer[5] << 40) |
                     ((uint64_t)buffer[4] << 32) |
                     ((uint64_t)buffer[3] << 24) |
                     ((uint64_t)buffer[2] << 16) |
                     ((uint64_t)buffer[1] << 8)  |
                     ((uint64_t)buffer[0]);

    #ifdef DEBUG
    fprintf(stderr, "Reading uint64:\n");
    fprintf(stderr, "Input bytes: %02x %02x %02x %02x %02x %02x %02x %02x\n",
            buffer[0], buffer[1], buffer[2], buffer[3],
            buffer[4], buffer[5], buffer[6], buffer[7]);
    fprintf(stderr, "Converted value: %" PRIu64 "\n", value);
    #endif

    return value;
}

void writeUint32(uint8_t* buffer, uint32_t value) {
    // Write in little-endian order
    buffer[0] = (uint8_t)(value);
    buffer[1] = (uint8_t)(value >> 8);
    buffer[2] = (uint8_t)(value >> 16);
    buffer[3] = (uint8_t)(value >> 24);
}

void writeUint64(uint8_t* buffer, uint64_t value) {
    // Write bytes in little-endian order
    buffer[0] = (uint8_t)(value);
    buffer[1] = (uint8_t)(value >> 8);
    buffer[2] = (uint8_t)(value >> 16);
    buffer[3] = (uint8_t)(value >> 24);
    buffer[4] = (uint8_t)(value >> 32);
    buffer[5] = (uint8_t)(value >> 40);
    buffer[6] = (uint8_t)(value >> 48);
    buffer[7] = (uint8_t)(value >> 56);
}

// Add message building implementation
MessageBuilder* createMessageBuilder(size_t initial_size, uint16_t max_part_size) {
    MessageBuilder* builder = malloc(sizeof(MessageBuilder));
    if (!builder) return NULL;

    builder->buffer = malloc(initial_size);
    if (!builder->buffer) {
        free(builder);
        return NULL;
    }

    builder->capacity = initial_size;
    builder->length = 0;
    builder->max_part_size = max_part_size;
    builder->part_count = 0;
    builder->seq = 0;  // Will be set when sending

    return builder;
}

bool addMessageData(MessageBuilder* builder, const void* data, size_t length) {
    if (builder->length + length > builder->capacity) {
        size_t new_capacity = builder->capacity * 2;
        uint8_t* new_buffer = realloc(builder->buffer, new_capacity);
        if (!new_buffer) return false;
        
        builder->buffer = new_buffer;
        builder->capacity = new_capacity;
    }

    memcpy(builder->buffer + builder->length, data, length);
    builder->length += length;
    builder->part_count = (builder->length + builder->max_part_size - 1) / builder->max_part_size;

    return true;
}

size_t getNextMessagePart(MessageBuilder* builder, uint8_t** part_buffer, MultiPartHeader* header) {
    static uint16_t current_part = 0;
    
    if (current_part >= builder->part_count) {
        current_part = 0;  // Reset for next message
        return 0;
    }

    size_t offset = current_part * builder->max_part_size;
    size_t remaining = builder->length - offset;
    size_t part_size = (remaining > builder->max_part_size) ? 
                       builder->max_part_size : remaining;

    // Set the base header fields
    header->header.type = (current_part == 0) ? MSG_FLAG_FIRST_PART : 0;
    if (current_part == builder->part_count - 1) {
        header->header.type |= MSG_FLAG_LAST_PART;
    }
    header->header.version = 1;
    header->header.length = part_size;
    header->header.sequence = builder->seq;  // Changed from seq to sequence

    // Set the multi-part specific fields
    header->part = current_part;
    header->parts = builder->part_count;
    
    *part_buffer = builder->buffer + offset;
    current_part++;

    return part_size;
}

// Add message assembly implementation
MessageAssembler* createMessageAssembler(void) {
    MessageAssembler* assembler = calloc(1, sizeof(MessageAssembler));
    return assembler;
}

bool addMessagePart(MessageAssembler* assembler, const MultiPartHeader* header, 
                   const uint8_t* data, size_t length) {
    // Validate part number
    if (header->part >= 256 || header->parts > 256) return false;
    
    // First part initialization
    if (header->header.type & MSG_FLAG_FIRST_PART) {
        assembler->type = header->header.type & MSG_TYPE_MASK;
        assembler->seq = header->header.sequence;  // Changed from seq to sequence
        assembler->expected_parts = header->parts;
        assembler->received_parts = 0;
    }
    
    // Verify sequence number matches
    if (header->header.sequence != assembler->seq) return false;  // Changed from seq to sequence

    // Store the part
    assembler->parts[header->part] = malloc(length);
    if (!assembler->parts[header->part]) return false;
    
    memcpy(assembler->parts[header->part], data, length);
    assembler->part_sizes[header->part] = length;
    assembler->received_parts++;

    return true;
}

// ... rest of assembler implementation ...

// ... rest of existing code ...
