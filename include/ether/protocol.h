/**
 * Ether Wire Protocol
 *
 * Binary protocol for client-server communication
 *
 * Copyright (C) 2024 - Licensed under GPL-3.0
 */

#ifndef ETHER_PROTOCOL_H
#define ETHER_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// CONSTANTS
// =============================================================================

#define ETHER_MAGIC         0xE7E7E7E7          // Protocol magic number
#define ETHER_PROTOCOL_VER  1                    // Protocol version
#define ETHER_DEFAULT_PORT  9999                 // Default server port
#define ETHER_MAX_PAYLOAD   (16 * 1024 * 1024)   // 16 MB max payload

// =============================================================================
// COMMANDS
// =============================================================================

typedef enum {
    // Health check
    ETHER_CMD_PING      = 0x01,   // Ping request
    ETHER_CMD_PONG      = 0x02,   // Pong response

    // Memory operations
    ETHER_CMD_ALLOC     = 0x10,   // Allocate memory
    ETHER_CMD_FREE      = 0x11,   // Free memory
    ETHER_CMD_REALLOC   = 0x12,   // Reallocate memory

    // Data operations
    ETHER_CMD_WRITE     = 0x20,   // Write to block
    ETHER_CMD_READ      = 0x21,   // Read from block

    // Responses
    ETHER_CMD_OK        = 0xF0,   // Success response
    ETHER_CMD_ERROR     = 0xFF,   // Error response
} ether_cmd_t;

// =============================================================================
// MESSAGE STRUCTURE
// =============================================================================

/**
 * Message header (24 bytes, fixed size)
 *
 * Wire format (network byte order):
 *
 * Offset  Size  Field
 * ------  ----  -----
 * 0       4     magic      - 0xE7E7E7E7
 * 4       1     version    - Protocol version (1)
 * 5       1     command    - Command type (ether_cmd_t)
 * 6       2     flags      - Reserved flags
 * 8       8     handle     - Block handle (64-bit)
 * 16      4     size       - Payload size
 * 20      4     reserved   - Future use
 */
typedef struct __attribute__((packed)) {
    uint32_t magic;       // ETHER_MAGIC
    uint8_t  version;     // Protocol version
    uint8_t  command;     // ether_cmd_t
    uint16_t flags;       // Reserved
    uint64_t handle;      // Block handle
    uint32_t size;        // Payload size
    uint32_t reserved;    // Padding/future use
} ether_msg_header_t;

#define ETHER_HEADER_SIZE sizeof(ether_msg_header_t)

/**
 * Complete message (header + variable payload)
 */
typedef struct {
    ether_msg_header_t header;
    uint8_t payload[];   // Flexible array member
} ether_msg_t;

// =============================================================================
// API
// =============================================================================

/**
 * Create a new message
 *
 * @param cmd           Command type
 * @param payload_size  Payload size in bytes
 * @return              Allocated message (free with ether_msg_free())
 */
ether_msg_t* ether_msg_create(ether_cmd_t cmd, size_t payload_size);

/**
 * Free a message
 *
 * @param msg   Message to free (can be NULL)
 */
void ether_msg_free(ether_msg_t* msg);

/**
 * Validate a message header
 *
 * @param header  Header to validate
 * @return        1 if valid, 0 if invalid
 */
int ether_msg_validate(const ether_msg_header_t* header);

/**
 * Get total message size (header + payload)
 *
 * @param msg   Message
 * @return      Total size in bytes
 */
size_t ether_msg_total_size(const ether_msg_t* msg);

/**
 * Serialize header to network byte order
 *
 * @param header  Header to serialize
 * @param buffer  Output buffer (must be ETHER_HEADER_SIZE bytes)
 */
void ether_msg_serialize_header(const ether_msg_header_t* header, uint8_t* buffer);

/**
 * Deserialize header from network byte order
 *
 * @param buffer  Input buffer (ETHER_HEADER_SIZE bytes)
 * @param header  Output header
 */
void ether_msg_deserialize_header(const uint8_t* buffer, ether_msg_header_t* header);

/**
 * Get command name as string (for debugging)
 *
 * @param cmd   Command
 * @return      Static string
 */
const char* ether_cmd_to_string(ether_cmd_t cmd);

/**
 * Print message contents (for debugging)
 *
 * @param msg   Message to print
 */
void ether_msg_dump(const ether_msg_t* msg);

#ifdef __cplusplus
}
#endif

#endif // ETHER_PROTOCOL_H