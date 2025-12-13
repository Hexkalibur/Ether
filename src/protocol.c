/**
 * Ether Wire Protocol Implementation
 *
 * Gestione messaggi client-server
 */

#include "ether/protocol.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <arpa/inet.h>  // htonl, ntohl

// =============================================================================
// MESSAGE CREATION
// =============================================================================

ether_msg_t *ether_msg_create(ether_cmd_t cmd, size_t payload_size) {
    if (payload_size > ETHER_MAX_PAYLOAD) {
        return NULL;
    }

    size_t total = sizeof(ether_msg_t) + payload_size;
    ether_msg_t *msg = (ether_msg_t *) calloc(1, total);

    if (!msg) {
        return NULL;
    }

    msg->header.magic = ETHER_MAGIC;
    msg->header.version = ETHER_PROTOCOL_VER;
    msg->header.command = (uint8_t) cmd;
    msg->header.flags = 0;
    msg->header.handle = 0;
    msg->header.size = (uint32_t) payload_size;
    msg->header.reserved = 0;

    return msg;
}

void ether_msg_free(ether_msg_t *msg) {
    free(msg);
}

// =============================================================================
// VALIDATION
// =============================================================================

int ether_msg_validate(const ether_msg_header_t *header) {
    if (!header) {
        return 0;
    }

    if (header->magic != ETHER_MAGIC) {
        return 0;
    }

    if (header->version != ETHER_PROTOCOL_VER) {
        return 0;
    }

    if (header->size > ETHER_MAX_PAYLOAD) {
        return 0;
    }

    return 1;
}

size_t ether_msg_total_size(const ether_msg_t *msg) {
    if (!msg) {
        return 0;
    }
    return ETHER_HEADER_SIZE + msg->header.size;
}

// =============================================================================
// SERIALIZATION (Network Byte Order)
// =============================================================================

void ether_msg_serialize_header(const ether_msg_header_t *header, uint8_t *buffer) {
    if (!header || !buffer) {
        return;
    }

    // Convert to network byte order (big-endian)
    uint32_t *buf32 = (uint32_t *) buffer;

    buf32[0] = htonl(header->magic);
    buffer[4] = header->version;
    buffer[5] = header->command;
    buffer[6] = (header->flags >> 8) & 0xFF;
    buffer[7] = header->flags & 0xFF;

    // Handle (64-bit) - manual conversion for portability
    uint64_t handle = header->handle;
    buffer[8] = (handle >> 56) & 0xFF;
    buffer[9] = (handle >> 48) & 0xFF;
    buffer[10] = (handle >> 40) & 0xFF;
    buffer[11] = (handle >> 32) & 0xFF;
    buffer[12] = (handle >> 24) & 0xFF;
    buffer[13] = (handle >> 16) & 0xFF;
    buffer[14] = (handle >> 8) & 0xFF;
    buffer[15] = handle & 0xFF;

    buf32 = (uint32_t *) (buffer + 16);
    buf32[0] = htonl(header->size);
    buf32[1] = htonl(header->reserved);
}

void ether_msg_deserialize_header(const uint8_t *buffer, ether_msg_header_t *header) {
    if (!buffer || !header) {
        return;
    }

    const uint32_t *buf32 = (const uint32_t *) buffer;

    header->magic = ntohl(buf32[0]);
    header->version = buffer[4];
    header->command = buffer[5];
    header->flags = ((uint16_t) buffer[6] << 8) | buffer[7];

    // Handle (64-bit)
    header->handle =
            ((uint64_t) buffer[8] << 56) |
            ((uint64_t) buffer[9] << 48) |
            ((uint64_t) buffer[10] << 40) |
            ((uint64_t) buffer[11] << 32) |
            ((uint64_t) buffer[12] << 24) |
            ((uint64_t) buffer[13] << 16) |
            ((uint64_t) buffer[14] << 8) |
            ((uint64_t) buffer[15]);

    buf32 = (const uint32_t *) (buffer + 16);
    header->size = ntohl(buf32[0]);
    header->reserved = ntohl(buf32[1]);
}

// =============================================================================
// DEBUG
// =============================================================================

static const char *cmd_to_string(uint8_t cmd) {
    switch (cmd) {
        case ETHER_CMD_PING: return "PING";
        case ETHER_CMD_PONG: return "PONG";
        case ETHER_CMD_ALLOC: return "ALLOC";
        case ETHER_CMD_FREE: return "FREE";
        case ETHER_CMD_REALLOC: return "REALLOC";
        case ETHER_CMD_WRITE: return "WRITE";
        case ETHER_CMD_READ: return "READ";
        case ETHER_CMD_OK: return "OK";
        case ETHER_CMD_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

void ether_msg_dump(const ether_msg_t *msg) {
    if (!msg) {
        printf("[NULL message]\n");
        return;
    }

    printf("=== Ether Message ===\n");
    printf("Magic:    0x%08X %s\n", msg->header.magic,
           msg->header.magic == ETHER_MAGIC ? "(valid)" : "(INVALID!)");
    printf("Version:  %d\n", msg->header.version);
    printf("Command:  0x%02X (%s)\n", msg->header.command,
           cmd_to_string(msg->header.command));
    printf("Flags:    0x%04X\n", msg->header.flags);
    printf("Handle:   0x%016lX\n", (unsigned long) msg->header.handle);
    printf("Size:     %u bytes\n", msg->header.size);

    if (msg->header.size > 0 && msg->header.size <= 64) {
        printf("Payload:  ");
        for (uint32_t i = 0; i < msg->header.size; i++) {
            printf("%02X ", msg->payload[i]);
        }
        printf("\n");
    }
    printf("=====================\n");
}