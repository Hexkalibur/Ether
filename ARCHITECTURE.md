# Architecture Overview

[Back to README](../README.md)

This document describes the overall architecture of Ether, how components interact, and the data flow through the system.

---

## System Overview

Ether follows a client-server architecture where clients request resources (memory, processes) from servers that manage those resources.

```
┌─────────────────────────────────────────────────────────────────┐
│                         APPLICATION                              │
│                                                                  │
│    void* ptr = ether_rmalloc(conn, 1024);                       │
│    ether_rwrite(conn, ptr, "hello", 6);                         │
│                                                                  │
└─────────────────────┬───────────────────────────────────────────┘
                      │
                      │ Function calls
                      ▼
┌─────────────────────────────────────────────────────────────────┐
│                     CLIENT LIBRARY                               │
│                     (libether_client)                            │
│                                                                  │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────────┐  │
│  │ Connection   │  │ Handle Cache │  │ Message Serialization│  │
│  │ Management   │  │ local -> rem │  │                      │  │
│  └──────────────┘  └──────────────┘  └──────────────────────┘  │
│                                                                  │
└─────────────────────┬───────────────────────────────────────────┘
                      │
                      │ TCP/IP (Binary Protocol)
                      ▼
┌─────────────────────────────────────────────────────────────────┐
│                     SERVER (etherd)                              │
│                                                                  │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────────┐  │
│  │ Socket Layer │  │ Handle Table │  │ Command Dispatcher   │  │
│  │              │  │ handle -> ptr│  │                      │  │
│  └──────────────┘  └──────────────┘  └──────────────────────┘  │
│                                                                  │
└─────────────────────┬───────────────────────────────────────────┘
                      │
                      │ Function calls
                      ▼
┌─────────────────────────────────────────────────────────────────┐
│                     CORE LIBRARY                                 │
│                     (libether)                                   │
│                                                                  │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────────┐  │
│  │ Allocator    │  │ Protocol     │  │ Statistics           │  │
│  │              │  │              │  │                      │  │
│  └──────────────┘  └──────────────┘  └──────────────────────┘  │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

---

## Component Relationships

### Libraries

There are two shared libraries:

1. **libether.so** - Core functionality
   - Memory allocator (ether_alloc, ether_free, etc.)
   - Protocol implementation (message creation, serialization)
   - No network code

2. **libether_client.so** - Client API
   - Links against libether
   - Connection management
   - Remote operations (ether_rmalloc, ether_rfree, etc.)
   - Handle caching

### Executables

1. **etherd** - Server daemon
   - Links against libether
   - TCP socket server
   - Handle mapping (server-side)
   - Command dispatch

2. **test_allocator** - Allocator tests
3. **test_protocol** - Protocol tests
4. **simple_client** - Example client application

### Dependency Graph

```
┌─────────────┐
│ Application │
└──────┬──────┘
       │ links
       ▼
┌──────────────────┐
│ libether_client  │
└────────┬─────────┘
         │ links
         ▼
┌─────────────┐     ┌─────────────┐
│  libether   │◄────│   etherd    │
└─────────────┘     └─────────────┘
```

---

## Data Flow

### Remote Allocation (ether_rmalloc)

```
CLIENT                                          SERVER
──────                                          ──────

1. User calls ether_rmalloc(conn, 100)
   │
2. │ Create ALLOC message
   │ header.command = ETHER_CMD_ALLOC
   │ header.size = 100
   │
3. │ Serialize to network byte order
   │
4. └──── [24B header] ─────────────────────────► recv()
                                                  │
5.                                          Deserialize header
                                                  │
6.                                          Validate (magic, version)
                                                  │
7.                                          ptr = ether_alloc(100)
                                                  │
8.                                          handle = store_handle(ptr)
                                                  │
9.                                          Create OK response
                                                  │
10. recv() ◄───────────────────────────── [24B header] ────┘
    │
11. Deserialize response
    │
12. local_ptr = calloc(100)
    │
13. cache_store(local_ptr, handle, 100)
    │
14. return local_ptr
```

### Remote Write (ether_rwrite)

```
CLIENT                                          SERVER
──────                                          ──────

1. User calls ether_rwrite(conn, ptr, data, len)
   │
2. │ handle = cache_lookup(ptr)
   │
3. │ Create WRITE message
   │ header.command = ETHER_CMD_WRITE
   │ header.handle = handle
   │ header.size = len
   │ payload = data
   │
4. └──── [24B header][payload] ────────────────► recv()
                                                  │
5.                                          Deserialize header
                                                  │
6.                                          recv() payload
                                                  │
7.                                          ptr = lookup_handle(handle)
                                                  │
8.                                          ether_write(ptr, payload, len)
                                                  │
9.                                          Create OK response
                                                  │
10. recv() ◄───────────────────────────── [24B header] ────┘
    │
11. Update local cache: memcpy(ptr, data, len)
    │
12. return ETHER_OK
```

---

## Memory Layout

### Allocator Block Structure

Each allocation has a hidden header preceding the user data:

```
Address:    0x1000              0x1018
            │                   │
            ▼                   ▼
            ┌───────────────────┬─────────────────────────────┐
            │  block_header_t   │      User Data              │
            │  (24 bytes)       │      (requested size)       │
            ├───────────────────┼─────────────────────────────┤
            │ magic:    4B      │                             │
            │ flags:    4B      │  <── Pointer returned       │
            │ size:     8B      │      to user                │
            │ capacity: 8B      │                             │
            └───────────────────┴─────────────────────────────┘
            │                   │
            │◄─── HEADER_SIZE ──►│
            │        (24)        │
```

### Handle Mapping

The server maintains a table mapping handles to actual pointers:

```
Client sees:          Server has:
────────────          ───────────

handle = 1    ─────►  g_handles[0] = {
                          handle: 1,
                          ptr: 0x7fff1000,
                          size: 100,
                          in_use: 1
                      }

handle = 2    ─────►  g_handles[1] = {
                          handle: 2,
                          ptr: 0x7fff2000,
                          size: 256,
                          in_use: 1
                      }
```

The client maintains a similar cache mapping local pointers to remote handles:

```
User sees:            Client cache:
──────────            ─────────────

local_ptr     ─────►  g_cache[0] = {
(0x1234)                  local_ptr: 0x1234,
                          remote_handle: 1,
                          size: 100,
                          valid: 1
                      }
```

---

## Network Protocol

### Message Structure

Every message has a fixed 24-byte header, optionally followed by a variable-length payload:

```
┌─────────────────────────────────────────────────────┐
│                   HEADER (24 bytes)                  │
├─────────┬─────────┬─────────┬───────────────────────┤
│ magic   │ ver cmd │ flags   │ handle                │
│ 4 bytes │ 1B  1B  │ 2 bytes │ 8 bytes               │
├─────────┴─────────┴─────────┼───────────────────────┤
│ size                        │ reserved              │
│ 4 bytes                     │ 4 bytes               │
└─────────────────────────────┴───────────────────────┘
┌─────────────────────────────────────────────────────┐
│                   PAYLOAD (variable)                 │
│                   (0 to 16MB)                        │
└─────────────────────────────────────────────────────┘
```

### Byte Order

All multi-byte fields are serialized in network byte order (big-endian) to ensure portability across architectures:

```
Host (little-endian x86):     Network (big-endian):
0x12345678                    0x12345678

Memory: 78 56 34 12           Memory: 12 34 56 78
        ▲                             ▲
        │                             │
        └── LSB first                 └── MSB first
```

---

## Threading Model

### Current: Single-Threaded

The current server is single-threaded and handles one client at a time:

```
while (running) {
    client = accept();      // Block until connection
    handle_client(client);  // Process all requests
    close(client);          // Client done
}
```

This is intentional for the MVP - it keeps the code simple and avoids race conditions.

### Future: Multi-Threaded or Async

Options for handling multiple clients:

1. **fork()** - One process per client
   - Simple to implement
   - Memory isolation between clients
   - Higher overhead

2. **pthread** - One thread per client
   - Lower overhead than fork
   - Requires mutex protection for shared state
   - Handle table needs locking

3. **libuv/epoll** - Event-driven async I/O
   - Single thread, multiple clients
   - Best performance
   - More complex code structure

---

## Security Considerations

### Memory Security

1. **Zero on Allocate** - All memory is zeroed before returning to user
2. **Secure Wipe on Free** - Memory is zeroed before being freed
3. **Magic Numbers** - Detect corruption and use-after-free

### Network Security

Current implementation has NO security features:
- No authentication
- No encryption
- No access control

These are intentionally deferred to focus on core functionality first. See [ROADMAP.md](ROADMAP.md) for planned security features.

---

## Error Handling

Errors are returned as negative integers:

```c
typedef enum {
    ETHER_OK          =  0,   // Success
    ETHER_ERR_NOMEM   = -1,   // Out of memory
    ETHER_ERR_INVALID = -2,   // Invalid argument
    ETHER_ERR_CORRUPT = -3,   // Memory corruption
    ETHER_ERR_OVERFLOW= -4,   // Buffer overflow
    ETHER_ERR_NETWORK = -5,   // Network error
    ETHER_ERR_TIMEOUT = -6,   // Timeout
    ETHER_ERR_NOTFOUND= -7,   // Handle not found
} ether_error_t;
```

The `ether_strerror()` function converts error codes to human-readable strings.

---

## Next Steps

See [ROADMAP.md](ROADMAP.md) for planned architectural changes:
- Process spawning subsystem
- Multi-node coordination
- Resource scheduling
