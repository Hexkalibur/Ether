# Wire Protocol

[Back to README](../README.md) | [Architecture](ARCHITECTURE.md)

This document specifies the Ether wire protocol used for client-server communication.

---

## Overview

Ether uses a custom binary protocol optimized for:

1. **Simplicity** - Fixed header size, minimal parsing
2. **Efficiency** - Binary encoding, no text overhead
3. **Portability** - Network byte order for cross-platform compatibility

---

## Message Format

Every message consists of a fixed 24-byte header, optionally followed by a variable-length payload.

```
┌─────────────────────────────────────────────────────────────────┐
│                         HEADER (24 bytes)                        │
├─────────┬─────────┬─────────┬─────────┬─────────┬───────────────┤
│  magic  │ version │ command │  flags  │        handle           │
│ 4 bytes │ 1 byte  │ 1 byte  │ 2 bytes │       8 bytes           │
├─────────┴─────────┴─────────┴─────────┼─────────────────────────┤
│              size                      │        reserved         │
│            4 bytes                     │        4 bytes          │
└────────────────────────────────────────┴─────────────────────────┘
┌─────────────────────────────────────────────────────────────────┐
│                         PAYLOAD (variable)                       │
│                         (0 to 16 MB)                             │
└─────────────────────────────────────────────────────────────────┘
```

---

## Header Fields

### magic (4 bytes)

Protocol identifier. Must be `0xE7E7E7E7`.

```c
#define ETHER_MAGIC 0xE7E7E7E7
```

Purpose:
- Identifies Ether protocol messages
- Detects stream corruption or desynchronization
- Rejects non-Ether connections

### version (1 byte)

Protocol version. Currently `1`.

```c
#define ETHER_PROTOCOL_VER 1
```

Allows for future protocol changes while maintaining backward compatibility.

### command (1 byte)

Operation type. See Command Reference below.

### flags (2 bytes)

Reserved for future use. Currently always `0`.

Potential uses:
- Compression flag
- Encryption flag
- Priority level

### handle (8 bytes)

64-bit identifier for memory blocks or processes.

- For ALLOC responses: The newly created handle
- For FREE/WRITE/READ requests: The target handle
- For PING/PONG: Unused (0)

### size (4 bytes)

For requests: Size of payload or requested allocation size
For responses: Size of payload

Maximum: 16 MB (`ETHER_MAX_PAYLOAD = 16 * 1024 * 1024`)

### reserved (4 bytes)

Reserved for future use. Always `0`.

---

## Byte Order

All multi-byte fields are transmitted in network byte order (big-endian).

### Example: Serializing magic = 0xE7E7E7E7

```
Host memory (little-endian x86):
  Address:  +0   +1   +2   +3
  Value:    E7   E7   E7   E7   (same either way for this value)

Wire format (big-endian):
  Byte 0: 0xE7 (most significant)
  Byte 1: 0xE7
  Byte 2: 0xE7
  Byte 3: 0xE7 (least significant)
```

### Example: Serializing handle = 0xDEADBEEFCAFEBABE

```
Host memory (little-endian x86):
  Address:  +0   +1   +2   +3   +4   +5   +6   +7
  Value:    BE   BA   FE   CA   EF   BE   AD   DE

Wire format (big-endian):
  Byte 0:  0xDE (most significant)
  Byte 1:  0xAD
  Byte 2:  0xBE
  Byte 3:  0xEF
  Byte 4:  0xCA
  Byte 5:  0xFE
  Byte 6:  0xBA
  Byte 7:  0xBE (least significant)
```

### Conversion Functions

```c
// 32-bit host to network
uint32_t wire_magic = htonl(header->magic);

// 32-bit network to host
header->magic = ntohl(wire_magic);

// 64-bit (manual, no standard function)
buffer[0] = (handle >> 56) & 0xFF;
buffer[1] = (handle >> 48) & 0xFF;
// ... etc
```

---

## Command Reference

### Health Check

| Command | Code | Description |
|---------|------|-------------|
| PING | 0x01 | Request server health check |
| PONG | 0x02 | Response to PING |

**PING Request:**
```
Header: magic=0xE7E7E7E7, version=1, command=0x01, 
        flags=0, handle=0, size=0, reserved=0
Payload: (none)
```

**PONG Response:**
```
Header: magic=0xE7E7E7E7, version=1, command=0x02,
        flags=0, handle=0, size=0, reserved=0
Payload: (none)
```

### Memory Operations

| Command | Code | Description |
|---------|------|-------------|
| ALLOC | 0x10 | Allocate memory |
| FREE | 0x11 | Free memory |
| REALLOC | 0x12 | Reallocate memory (not yet implemented) |

**ALLOC Request:**
```
Header: command=0x10, size=<bytes to allocate>
Payload: (none)
```

**ALLOC Response (success):**
```
Header: command=0xF0 (OK), handle=<new handle>
Payload: (none)
```

**FREE Request:**
```
Header: command=0x11, handle=<handle to free>
Payload: (none)
```

**FREE Response:**
```
Header: command=0xF0 (OK) or 0xFF (ERROR), handle=<same handle>
Payload: (none)
```

### Data Operations

| Command | Code | Description |
|---------|------|-------------|
| WRITE | 0x20 | Write data to memory block |
| READ | 0x21 | Read data from memory block |

**WRITE Request:**
```
Header: command=0x20, handle=<target>, size=<data length>
Payload: <data to write>
```

**WRITE Response:**
```
Header: command=0xF0 (OK) or 0xFF (ERROR)
Payload: (none)
```

**READ Request:**
```
Header: command=0x21, handle=<target>, size=<bytes to read>
Payload: (none)
```

**READ Response (success):**
```
Header: command=0xF0 (OK), size=<actual bytes read>
Payload: <data>
```

### Response Codes

| Command | Code | Description |
|---------|------|-------------|
| OK | 0xF0 | Operation succeeded |
| ERROR | 0xFF | Operation failed |

---

## Message Flow Examples

### Example 1: Allocate 1024 bytes

```
CLIENT --> SERVER

Header (24 bytes):
  Offset 0-3:   E7 E7 E7 E7     (magic)
  Offset 4:     01              (version)
  Offset 5:     10              (command: ALLOC)
  Offset 6-7:   00 00           (flags)
  Offset 8-15:  00 00 00 00 00 00 00 00  (handle: unused)
  Offset 16-19: 00 00 04 00     (size: 1024 in big-endian)
  Offset 20-23: 00 00 00 00     (reserved)

Payload: (none)


SERVER --> CLIENT

Header (24 bytes):
  Offset 0-3:   E7 E7 E7 E7     (magic)
  Offset 4:     01              (version)
  Offset 5:     F0              (command: OK)
  Offset 6-7:   00 00           (flags)
  Offset 8-15:  00 00 00 00 00 00 00 01  (handle: 1)
  Offset 16-19: 00 00 00 00     (size: 0)
  Offset 20-23: 00 00 00 00     (reserved)

Payload: (none)
```

### Example 2: Write "Hello" to handle 1

```
CLIENT --> SERVER

Header (24 bytes):
  Offset 5:     20              (command: WRITE)
  Offset 8-15:  00 00 00 00 00 00 00 01  (handle: 1)
  Offset 16-19: 00 00 00 06     (size: 6 bytes including null)
  ... (other fields as above)

Payload (6 bytes):
  48 65 6C 6C 6F 00             ("Hello\0")


SERVER --> CLIENT

Header (24 bytes):
  Offset 5:     F0              (command: OK)
  ... (other fields)

Payload: (none)
```

---

## Validation

Messages are validated on receipt:

```c
int ether_msg_validate(const ether_msg_header_t* header) {
    if (!header) return 0;
    
    // Check magic number
    if (header->magic != ETHER_MAGIC) return 0;
    
    // Check version
    if (header->version != ETHER_PROTOCOL_VER) return 0;
    
    // Check payload size
    if (header->size > ETHER_MAX_PAYLOAD) return 0;
    
    return 1;
}
```

Invalid messages are:
- Logged for debugging
- Discarded (no response sent for truly malformed messages)
- May trigger ERROR response for valid-but-failed operations

---

## Serialization Implementation

### Serialize Header

```c
void ether_msg_serialize_header(const ether_msg_header_t* header, 
                                 uint8_t* buffer) {
    // Magic (4 bytes, network order)
    uint32_t* buf32 = (uint32_t*)buffer;
    buf32[0] = htonl(header->magic);
    
    // Version, command (single bytes)
    buffer[4] = header->version;
    buffer[5] = header->command;
    
    // Flags (2 bytes, big-endian)
    buffer[6] = (header->flags >> 8) & 0xFF;
    buffer[7] = header->flags & 0xFF;
    
    // Handle (8 bytes, big-endian, manual)
    uint64_t h = header->handle;
    buffer[8]  = (h >> 56) & 0xFF;
    buffer[9]  = (h >> 48) & 0xFF;
    buffer[10] = (h >> 40) & 0xFF;
    buffer[11] = (h >> 32) & 0xFF;
    buffer[12] = (h >> 24) & 0xFF;
    buffer[13] = (h >> 16) & 0xFF;
    buffer[14] = (h >> 8)  & 0xFF;
    buffer[15] = h & 0xFF;
    
    // Size, reserved (4 bytes each)
    buf32 = (uint32_t*)(buffer + 16);
    buf32[0] = htonl(header->size);
    buf32[1] = htonl(header->reserved);
}
```

### Deserialize Header

```c
void ether_msg_deserialize_header(const uint8_t* buffer,
                                   ether_msg_header_t* header) {
    const uint32_t* buf32 = (const uint32_t*)buffer;
    
    header->magic = ntohl(buf32[0]);
    header->version = buffer[4];
    header->command = buffer[5];
    header->flags = ((uint16_t)buffer[6] << 8) | buffer[7];
    
    header->handle = 
        ((uint64_t)buffer[8]  << 56) |
        ((uint64_t)buffer[9]  << 48) |
        ((uint64_t)buffer[10] << 40) |
        ((uint64_t)buffer[11] << 32) |
        ((uint64_t)buffer[12] << 24) |
        ((uint64_t)buffer[13] << 16) |
        ((uint64_t)buffer[14] << 8)  |
        ((uint64_t)buffer[15]);
    
    buf32 = (const uint32_t*)(buffer + 16);
    header->size = ntohl(buf32[0]);
    header->reserved = ntohl(buf32[1]);
}
```

---

## Future Commands

Reserved command ranges for future features:

| Range | Purpose |
|-------|---------|
| 0x01-0x0F | Health/Status |
| 0x10-0x1F | Memory operations |
| 0x20-0x2F | Data operations |
| 0x30-0x3F | Process operations (planned) |
| 0x40-0x4F | Node/cluster operations (planned) |
| 0xF0-0xFF | Responses |

Planned commands:
```c
ETHER_CMD_SPAWN       = 0x30  // Start remote process
ETHER_CMD_KILL        = 0x31  // Kill remote process
ETHER_CMD_STATUS      = 0x32  // Get process status
ETHER_CMD_OUTPUT      = 0x33  // Get process stdout/stderr

ETHER_CMD_NODE_JOIN   = 0x40  // Node joins cluster
ETHER_CMD_NODE_LEAVE  = 0x41  // Node leaves cluster
ETHER_CMD_HEARTBEAT   = 0x42  // Keep-alive
ETHER_CMD_RESOURCES   = 0x43  // Report available resources
```

---

## API Reference

```c
// Create a message with given command and payload size
ether_msg_t* ether_msg_create(ether_cmd_t cmd, size_t payload_size);

// Free a message
void ether_msg_free(ether_msg_t* msg);

// Validate a header
int ether_msg_validate(const ether_msg_header_t* header);

// Get total message size (header + payload)
size_t ether_msg_total_size(const ether_msg_t* msg);

// Serialize header to buffer (network byte order)
void ether_msg_serialize_header(const ether_msg_header_t* header, 
                                 uint8_t* buffer);

// Deserialize header from buffer
void ether_msg_deserialize_header(const uint8_t* buffer,
                                   ether_msg_header_t* header);

// Get command name as string
const char* ether_cmd_to_string(ether_cmd_t cmd);

// Print message for debugging
void ether_msg_dump(const ether_msg_t* msg);
```

---

## Constants

```c
#define ETHER_MAGIC         0xE7E7E7E7
#define ETHER_PROTOCOL_VER  1
#define ETHER_DEFAULT_PORT  9999
#define ETHER_MAX_PAYLOAD   (16 * 1024 * 1024)  // 16 MB
#define ETHER_HEADER_SIZE   24
```
