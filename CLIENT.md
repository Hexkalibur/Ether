# Client Library

[Back to README](../README.md) | [Architecture](ARCHITECTURE.md) | [Protocol](PROTOCOL.md)

This document describes the Ether client library, which provides the user-facing API for remote memory operations.

---

## Overview

The client library provides:

1. **Connection Management** - Connect/disconnect from servers
2. **Remote Memory API** - rmalloc, rfree, rwrite, rread
3. **Handle Caching** - Map local pointers to remote handles
4. **Protocol Handling** - Message serialization and communication

---

## Connection Structure

```c
struct ether_conn {
    int  socket;        // TCP socket file descriptor
    char host[256];     // Server hostname (for reconnection)
    int  port;          // Server port
    int  connected;     // Connection status flag
};
```

The structure is opaque to users - they only see `ether_conn_t*`.

---

## Handle Cache

### The Problem

When the user calls `ether_rmalloc()`, they expect a pointer they can use. But the server returns a handle (just a number). We need to bridge this gap.

### The Solution

The client maintains a cache mapping local pointers to remote handles:

```c
typedef struct {
    void*    local_ptr;      // What user sees
    uint64_t remote_handle;  // What server knows
    size_t   size;           // Allocation size
    int      valid;          // Is entry in use?
} handle_cache_entry_t;

#define MAX_HANDLES 4096
static handle_cache_entry_t g_cache[MAX_HANDLES];
```

### How It Works

```
User calls: ether_rmalloc(conn, 100)

1. Client sends ALLOC(100) to server
2. Server allocates memory, returns handle=1
3. Client allocates local buffer: calloc(100)
4. Client stores mapping: local_ptr -> handle 1
5. Client returns local_ptr to user

User now has a pointer that:
- Points to local memory (for convenience)
- Is secretly mapped to handle 1 on server
```

---

## API Reference

### Connection Functions

```c
// Connect to server
// Returns: connection handle, or NULL on failure
ether_conn_t* ether_connect(const char* host, int port);

// Disconnect and free resources
// Safe to call with NULL
void ether_disconnect(ether_conn_t* conn);

// Check if server is alive
// Returns: 0 on success, -1 on failure
int ether_ping(ether_conn_t* conn);
```

### Memory Functions

```c
// Allocate remote memory
// Returns: local pointer (handle), or NULL on failure
void* ether_rmalloc(ether_conn_t* conn, size_t size);

// Free remote memory
// Safe to call with NULL pointer
void ether_rfree(ether_conn_t* conn, void* ptr);

// Write data to remote memory
// Returns: ETHER_OK or error code
int ether_rwrite(ether_conn_t* conn, void* ptr, const void* data, size_t len);

// Read data from remote memory
// Returns: ETHER_OK or error code
int ether_rread(ether_conn_t* conn, void* ptr, void* buffer, size_t len);

// Get size of remote allocation
// Returns: size in bytes, or 0 on error
size_t ether_rsize(ether_conn_t* conn, void* ptr);
```

---

## Error Handling

All functions indicate errors through return values:

```c
// Connection functions return NULL on failure
ether_conn_t* conn = ether_connect("localhost", 9999);
if (!conn) {
    fprintf(stderr, "Connection failed\n");
    exit(1);
}

// Memory allocation returns NULL on failure
void* ptr = ether_rmalloc(conn, 1024);
if (!ptr) {
    fprintf(stderr, "Allocation failed\n");
}

// Read/write return error codes
int ret = ether_rwrite(conn, ptr, data, len);
if (ret != ETHER_OK) {
    fprintf(stderr, "Write failed: %s\n", ether_strerror(ret));
}

// Possible error codes:
// ETHER_OK          (0)  - Success
// ETHER_ERR_INVALID (-2) - Invalid argument
// ETHER_ERR_OVERFLOW(-4) - Data too large for block
// ETHER_ERR_NETWORK (-5) - Network communication error
// ETHER_ERR_NOTFOUND(-7) - Handle not in cache
```

---

## Complete Usage Example

```c
#include "ether/client.h"
#include "ether/ether.h"
#include <stdio.h>
#include <string.h>

int main() {
    // 1. Connect to server
    printf("Connecting to server...\n");
    ether_conn_t* conn = ether_connect("localhost", 9999);
    if (!conn) {
        fprintf(stderr, "Failed to connect\n");
        return 1;
    }
    printf("Connected!\n");

    // 2. Check server health
    printf("Pinging server...\n");
    if (ether_ping(conn) != 0) {
        fprintf(stderr, "Server not responding\n");
        ether_disconnect(conn);
        return 1;
    }
    printf("Server is alive\n");

    // 3. Allocate remote memory
    printf("Allocating 256 bytes...\n");
    void* ptr = ether_rmalloc(conn, 256);
    if (!ptr) {
        fprintf(stderr, "Allocation failed\n");
        ether_disconnect(conn);
        return 1;
    }
    printf("Got handle: %p\n", ptr);

    // 4. Write data
    const char* message = "Hello from remote memory!";
    printf("Writing: \"%s\"\n", message);
    int ret = ether_rwrite(conn, ptr, message, strlen(message) + 1);
    if (ret != ETHER_OK) {
        fprintf(stderr, "Write failed: %s\n", ether_strerror(ret));
    }

    // 5. Read data back
    printf("Reading back...\n");
    char buffer[256] = {0};
    ret = ether_rread(conn, ptr, buffer, 256);
    if (ret == ETHER_OK) {
        printf("Read: \"%s\"\n", buffer);
    } else {
        fprintf(stderr, "Read failed: %s\n", ether_strerror(ret));
    }

    // 6. Verify
    if (strcmp(message, buffer) == 0) {
        printf("Data integrity verified!\n");
    }

    // 7. Free remote memory
    printf("Freeing remote memory...\n");
    ether_rfree(conn, ptr);

    // 8. Disconnect
    printf("Disconnecting...\n");
    ether_disconnect(conn);

    printf("Done!\n");
    return 0;
}
```

---

## Implementation Details

### Local Buffer Purpose

When `ether_rmalloc()` returns, the user gets a pointer to a local buffer:

```c
void* local_ptr = calloc(1, size);
```

This serves two purposes:

1. **Familiar Interface** - Users get a real pointer they can pass around
2. **Write Caching** - After `ether_rwrite()`, data is copied to local buffer

```c
// In ether_rwrite():
memcpy(ptr, data, len);  // Update local cache
```

This means after a write, the local buffer mirrors remote data. However, this is not a full cache - reads always go to the server.

### Global Cache Limitation

The handle cache is global, not per-connection:

```c
static handle_cache_entry_t g_cache[MAX_HANDLES];
```

This means:
- All connections share the same cache
- Handle collisions are possible if using multiple connections
- Not thread-safe

Future improvement: Per-connection cache stored in `ether_conn_t`.

### Missing Cleanup

Current implementation does not:
- Free remote allocations on disconnect
- Track which handles belong to which connection
- Handle server disconnection gracefully

These are known limitations of the MVP.

---

## Building Applications

### Compile with Client Library

```bash
gcc -o myapp myapp.c -L/path/to/build -lether_client -lether
```

### CMake Integration

```cmake
add_executable(myapp myapp.c)
target_link_libraries(myapp ether_client)
```

### Runtime Library Path

```bash
# Option 1: Install libraries
sudo make install

# Option 2: Set LD_LIBRARY_PATH
export LD_LIBRARY_PATH=/path/to/build:$LD_LIBRARY_PATH
./myapp

# Option 3: Use rpath
gcc -o myapp myapp.c -Wl,-rpath,/path/to/build -lether_client
```

---

## Debugging Tips

### Enable Debug Output

```c
// In your application:
ether_set_debug(true);

// Now allocator operations will print to stderr:
// [ETHER] alloc OK: ptr=0x1234 size=100
// [ETHER] write OK: ptr=0x1234 len=50
// [ETHER] free OK: ptr=0x1234 size=100
```

### Check Connection State

```c
// Ping before critical operations
if (ether_ping(conn) != 0) {
    // Reconnect or abort
    ether_disconnect(conn);
    conn = ether_connect(host, port);
}
```

### Verify Allocations

```c
void* ptr = ether_rmalloc(conn, size);
if (!ptr) {
    // Check if it's a connection issue
    if (ether_ping(conn) != 0) {
        fprintf(stderr, "Server disconnected\n");
    } else {
        fprintf(stderr, "Allocation failed (out of memory?)\n");
    }
}
```

---

## Future Improvements

1. **Per-Connection Cache** - Move cache into connection structure
2. **Automatic Reconnection** - Retry on transient failures
3. **Connection Pooling** - Reuse connections across threads
4. **Async Operations** - Non-blocking API with callbacks
5. **Batch Operations** - Multiple operations in single round-trip
6. **Compression** - Compress large payloads
7. **Encryption** - TLS for secure communication
