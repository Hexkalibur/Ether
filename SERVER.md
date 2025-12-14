# Server Implementation

[Back to README](../README.md) | [Architecture](ARCHITECTURE.md) | [Protocol](PROTOCOL.md)

This document describes the etherd server daemon, how it handles connections, and manages memory on behalf of clients.

---

## Overview

The etherd daemon is a TCP server that:

1. Listens for client connections
2. Receives protocol messages
3. Executes memory operations
4. Sends responses

```
┌─────────────────────────────────────────────────────────────────┐
│                         etherd                                   │
│                                                                  │
│  ┌─────────────┐   ┌─────────────┐   ┌─────────────────────┐   │
│  │   Socket    │   │   Handle    │   │    Command          │   │
│  │   Layer     │──►│   Table     │──►│    Handlers         │   │
│  │             │   │             │   │                     │   │
│  │ accept()    │   │ handle->ptr │   │ PING, ALLOC, FREE,  │   │
│  │ recv()      │   │             │   │ WRITE, READ         │   │
│  │ send()      │   │             │   │                     │   │
│  └─────────────┘   └─────────────┘   └─────────────────────┘   │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

---

## Handle Table

Clients never see real memory addresses. Instead, they receive opaque "handles" that the server maps to actual pointers.

### Why Handles?

1. **Security** - Clients cannot manipulate or guess memory addresses
2. **Abstraction** - Handle 5 on server A is independent of handle 5 on server B
3. **Flexibility** - Handles could survive server restarts (future feature)

### Implementation

```c
typedef struct {
    uint64_t handle;    // Client-visible identifier (1, 2, 3, ...)
    void*    ptr;       // Actual memory pointer
    size_t   size;      // Allocation size
    int      in_use;    // Is this slot active?
} handle_entry_t;

#define MAX_HANDLES 4096
static handle_entry_t g_handles[MAX_HANDLES];
static uint64_t g_next_handle = 1;
```

### Operations

**Store Handle:**
```c
static uint64_t store_handle(void* ptr, size_t size) {
    for (int i = 0; i < MAX_HANDLES; i++) {
        if (!g_handles[i].in_use) {
            g_handles[i].handle = g_next_handle++;
            g_handles[i].ptr = ptr;
            g_handles[i].size = size;
            g_handles[i].in_use = 1;
            return g_handles[i].handle;
        }
    }
    return 0;  // No space
}
```

**Lookup Handle:**
```c
static void* lookup_handle(uint64_t handle, size_t* size) {
    for (int i = 0; i < MAX_HANDLES; i++) {
        if (g_handles[i].in_use && g_handles[i].handle == handle) {
            if (size) *size = g_handles[i].size;
            return g_handles[i].ptr;
        }
    }
    return NULL;
}
```

**Remove Handle:**
```c
static int remove_handle(uint64_t handle) {
    for (int i = 0; i < MAX_HANDLES; i++) {
        if (g_handles[i].in_use && g_handles[i].handle == handle) {
            g_handles[i].in_use = 0;
            g_handles[i].ptr = NULL;
            return 1;
        }
    }
    return 0;
}
```

### Limitations

- Fixed size array (4096 handles max)
- Linear search O(n)
- Not thread-safe

Future improvements:
- Hash table for O(1) lookup
- Dynamic sizing
- Per-client handle tables
- Mutex protection

---

## Socket Setup

```c
int main(int argc, char** argv) {
    int port = ETHER_DEFAULT_PORT;  // 9999
    
    // 1. Create TCP socket
    g_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    
    // 2. Allow address reuse (for quick restarts)
    int opt = 1;
    setsockopt(g_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // 3. Bind to all interfaces on port
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,  // 0.0.0.0
        .sin_port = htons(port)
    };
    bind(g_server_fd, (struct sockaddr*)&addr, sizeof(addr));
    
    // 4. Start listening
    listen(g_server_fd, MAX_CLIENTS);  // 64 queued connections
    
    // 5. Accept loop
    while (g_running) {
        int client_fd = accept(g_server_fd, ...);
        handle_client(client_fd);
    }
}
```

### Signal Handling

```c
static volatile int g_running = 1;

static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
}

// In main():
signal(SIGINT, signal_handler);   // Ctrl+C
signal(SIGTERM, signal_handler);  // kill
signal(SIGPIPE, SIG_IGN);         // Ignore broken pipe
```

SIGPIPE handling is important: without `SIG_IGN`, the server would crash when a client disconnects unexpectedly during a write.

---

## Client Handling

```c
static void handle_client(int client_fd) {
    uint8_t header_buf[ETHER_HEADER_SIZE];
    uint8_t* payload = NULL;

    while (g_running) {
        // 1. Read header (exactly 24 bytes)
        ssize_t n = recv(client_fd, header_buf, ETHER_HEADER_SIZE, MSG_WAITALL);
        if (n <= 0) break;  // Client disconnected

        // 2. Deserialize
        ether_msg_header_t header;
        ether_msg_deserialize_header(header_buf, &header);

        // 3. Validate
        if (!ether_msg_validate(&header)) {
            continue;  // Invalid message, skip
        }

        // 4. Read payload if present
        if (header.size > 0) {
            payload = malloc(header.size);
            recv(client_fd, payload, header.size, MSG_WAITALL);
        }

        // 5. Dispatch command
        switch (header.command) {
            case ETHER_CMD_PING:  handle_ping(client_fd); break;
            case ETHER_CMD_ALLOC: handle_alloc(client_fd, &header); break;
            case ETHER_CMD_FREE:  handle_free(client_fd, &header); break;
            case ETHER_CMD_WRITE: handle_write(client_fd, &header, payload); break;
            case ETHER_CMD_READ:  handle_read(client_fd, &header); break;
            default:
                send_response(client_fd, ETHER_CMD_ERROR, 0, NULL, 0);
        }

        // 6. Cleanup
        if (payload) { free(payload); payload = NULL; }
    }

    close(client_fd);
}
```

### MSG_WAITALL Flag

```c
recv(client_fd, header_buf, ETHER_HEADER_SIZE, MSG_WAITALL);
```

`MSG_WAITALL` tells recv() to block until exactly the requested number of bytes are received. Without it, TCP might deliver partial data:

```
Without MSG_WAITALL:
  Request: recv(fd, buf, 24, 0)
  Result:  16 bytes received (partial)
  Problem: Must loop to get remaining 8 bytes

With MSG_WAITALL:
  Request: recv(fd, buf, 24, MSG_WAITALL)
  Result:  24 bytes received (complete)
  Benefit: Simpler code, no loop needed
```

---

## Command Handlers

### PING Handler

```c
static void handle_ping(int client_fd) {
    printf("[etherd] PING received\n");
    send_response(client_fd, ETHER_CMD_PONG, 0, NULL, 0);
}
```

### ALLOC Handler

```c
static void handle_alloc(int client_fd, ether_msg_header_t* header) {
    size_t size = header->size;
    printf("[etherd] ALLOC request: %zu bytes\n", size);

    // 1. Allocate memory
    void* ptr = ether_alloc(size);
    if (!ptr) {
        send_response(client_fd, ETHER_CMD_ERROR, 0, NULL, 0);
        return;
    }

    // 2. Create handle mapping
    uint64_t handle = store_handle(ptr, size);
    if (handle == 0) {
        ether_free(ptr);
        send_response(client_fd, ETHER_CMD_ERROR, 0, NULL, 0);
        return;
    }

    // 3. Send success with handle
    printf("[etherd] ALLOC OK: handle=0x%lX ptr=%p\n", handle, ptr);
    send_response(client_fd, ETHER_CMD_OK, handle, NULL, 0);
}
```

### FREE Handler

```c
static void handle_free(int client_fd, ether_msg_header_t* header) {
    uint64_t handle = header->handle;
    printf("[etherd] FREE request: handle=0x%lX\n", handle);

    // 1. Lookup handle
    void* ptr = lookup_handle(handle, NULL);
    if (!ptr) {
        send_response(client_fd, ETHER_CMD_ERROR, handle, NULL, 0);
        return;
    }

    // 2. Free memory
    ether_free(ptr);
    
    // 3. Remove handle mapping
    remove_handle(handle);

    // 4. Send success
    send_response(client_fd, ETHER_CMD_OK, handle, NULL, 0);
}
```

### WRITE Handler

```c
static void handle_write(int client_fd, ether_msg_header_t* header,
                         const uint8_t* payload) {
    uint64_t handle = header->handle;
    size_t len = header->size;

    // 1. Lookup handle
    size_t block_size;
    void* ptr = lookup_handle(handle, &block_size);
    if (!ptr) {
        send_response(client_fd, ETHER_CMD_ERROR, handle, NULL, 0);
        return;
    }

    // 2. Bounds check
    if (len > block_size) {
        send_response(client_fd, ETHER_CMD_ERROR, handle, NULL, 0);
        return;
    }

    // 3. Write data
    int ret = ether_write(ptr, payload, len);
    if (ret != ETHER_OK) {
        send_response(client_fd, ETHER_CMD_ERROR, handle, NULL, 0);
        return;
    }

    // 4. Send success
    send_response(client_fd, ETHER_CMD_OK, handle, NULL, 0);
}
```

### READ Handler

```c
static void handle_read(int client_fd, ether_msg_header_t* header) {
    uint64_t handle = header->handle;
    size_t len = header->size;

    // 1. Lookup handle
    size_t block_size;
    void* ptr = lookup_handle(handle, &block_size);
    if (!ptr) {
        send_response(client_fd, ETHER_CMD_ERROR, handle, NULL, 0);
        return;
    }

    // 2. Cap to available size
    if (len > block_size) {
        len = block_size;
    }

    // 3. Read data
    uint8_t* buffer = malloc(len);
    int ret = ether_read(ptr, buffer, len);
    if (ret != ETHER_OK) {
        free(buffer);
        send_response(client_fd, ETHER_CMD_ERROR, handle, NULL, 0);
        return;
    }

    // 4. Send data
    send_response(client_fd, ETHER_CMD_OK, handle, buffer, len);
    free(buffer);
}
```

---

## Response Helper

```c
static void send_response(int client_fd, ether_cmd_t cmd, uint64_t handle,
                          const void* data, size_t data_len) {
    // 1. Create message
    ether_msg_t* response = ether_msg_create(cmd, data_len);
    response->header.handle = handle;
    
    if (data && data_len > 0) {
        memcpy(response->payload, data, data_len);
    }

    // 2. Serialize header
    uint8_t header_buf[ETHER_HEADER_SIZE];
    ether_msg_serialize_header(&response->header, header_buf);

    // 3. Send header
    send(client_fd, header_buf, ETHER_HEADER_SIZE, 0);

    // 4. Send payload if present
    if (data_len > 0) {
        send(client_fd, response->payload, data_len, 0);
    }

    ether_msg_free(response);
}
```

---

## Threading Model

### Current: Single-Threaded

The server handles one client at a time:

```
Time
  │
  ▼
  accept(client_A)
  │
  ├── handle requests from A
  │   ├── PING
  │   ├── ALLOC
  │   └── ...
  │
  close(client_A)
  │
  accept(client_B)   <-- B had to wait
  │
  ...
```

### Why Single-Threaded?

1. **Simplicity** - No mutex, no race conditions
2. **Correctness** - Easier to reason about
3. **Learning** - Focus on protocol, not concurrency

### Future: Multi-Client Options

**Option 1: fork()**
```c
while (running) {
    int client = accept(...);
    if (fork() == 0) {
        // Child process
        handle_client(client);
        exit(0);
    }
    close(client);  // Parent closes its copy
}
```

Pros: Simple, memory isolation
Cons: High overhead, handle table not shared

**Option 2: pthreads**
```c
while (running) {
    int client = accept(...);
    pthread_create(&thread, NULL, handle_client, client);
}
```

Pros: Lower overhead, shared memory
Cons: Needs mutex for handle table

**Option 3: Event loop (libuv/epoll)**
```c
// Single thread, multiple clients via async I/O
while (running) {
    events = epoll_wait(...);
    for (event in events) {
        if (event.is_accept) accept_new_client();
        if (event.is_read) process_client_data();
    }
}
```

Pros: Best performance, single thread
Cons: More complex code structure

---

## Logging

Current logging is simple printf:

```
[etherd] Client connected from 127.0.0.1:54321
[etherd] PING received
[etherd] ALLOC request: 1024 bytes
[etherd] ALLOC OK: handle=0x1 ptr=0x7fff1000
[etherd] WRITE request: handle=0x1 len=100
[etherd] WRITE OK
[etherd] Client disconnected
```

Future improvements:
- Log levels (DEBUG, INFO, WARN, ERROR)
- Timestamps
- File output
- Structured logging (JSON)

---

## Startup Banner

```
===========================================
  Ether Daemon v0.1.0
  Distributed Resource Allocation System
===========================================
Listening on 0.0.0.0:9999
Max handles: 4096
Press Ctrl+C to stop
```

---

## Shutdown

On SIGINT/SIGTERM:

1. Set `g_running = 0`
2. Accept loop exits
3. Close server socket
4. Print final statistics
5. Exit

```
^C
[etherd] Received signal, shutting down...

[etherd] Final statistics:
=== Ether Allocator State ===
Total allocated: 10240 bytes
Total freed:     10240 bytes
Current usage:   0 bytes
Peak usage:      4096 bytes
Allocations:     10
Frees:           10
=============================

[etherd] Goodbye!
```

Note: Currently does not clean up client connections or allocated memory. A production version would:
- Track active clients
- Free all allocations on shutdown
- Wait for clients to disconnect gracefully

---

## Command Line

```bash
# Default port (9999)
./etherd

# Custom port
./etherd 8888
```

Future options:
```bash
./etherd --port 8888
./etherd --config /etc/etherd.conf
./etherd --log-level debug
./etherd --daemon
```
