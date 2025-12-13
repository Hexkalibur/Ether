/**
 * Ether Client Library
 *
 * Provides rmalloc(), rfree(), rwrite(), rread() for
 * allocating memory on a remote Ether server.
 *
 * Copyright (C) 2024 - Licensed under GPL-3.0
 */

#include "ether/protocol.h"
#include "ether/client.h"
#include "ether/ether.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

// =============================================================================
// CONNECTION STRUCTURE
// =============================================================================

struct ether_conn {
    int  socket;        // TCP socket file descriptor
    char host[256];     // Server hostname
    int  port;          // Server port
    int  connected;     // Connection status flag
};

// =============================================================================
// HANDLE CACHE
// =============================================================================

/**
 * Local cache mapping local pointers to remote handles.
 *
 * When client calls rmalloc(), we:
 * 1. Ask server to allocate memory, get back a handle
 * 2. Allocate a local buffer for convenience
 * 3. Store mapping: local_ptr -> remote_handle
 */
typedef struct {
    void*    local_ptr;      // Local buffer (what user sees)
    uint64_t remote_handle;  // Server-side handle
    size_t   size;           // Allocation size
    int      valid;          // Is this entry in use?
} handle_cache_entry_t;

#define MAX_HANDLES 4096
static handle_cache_entry_t g_cache[MAX_HANDLES];

/**
 * Store a mapping in the cache
 */
static int cache_store(void* local, uint64_t remote, size_t size) {
    for (int i = 0; i < MAX_HANDLES; i++) {
        if (!g_cache[i].valid) {
            g_cache[i].local_ptr = local;
            g_cache[i].remote_handle = remote;
            g_cache[i].size = size;
            g_cache[i].valid = 1;
            return 0;
        }
    }
    return -1;  // Cache full
}

/**
 * Look up remote handle by local pointer
 */
static uint64_t cache_lookup(void* local, size_t* size) {
    for (int i = 0; i < MAX_HANDLES; i++) {
        if (g_cache[i].valid && g_cache[i].local_ptr == local) {
            if (size) *size = g_cache[i].size;
            return g_cache[i].remote_handle;
        }
    }
    return 0;  // Not found
}

/**
 * Remove entry from cache
 */
static int cache_remove(void* local) {
    for (int i = 0; i < MAX_HANDLES; i++) {
        if (g_cache[i].valid && g_cache[i].local_ptr == local) {
            g_cache[i].valid = 0;
            return 0;
        }
    }
    return -1;  // Not found
}

// =============================================================================
// INTERNAL HELPERS
// =============================================================================

/**
 * Send a message to the server
 */
static int send_message(ether_conn_t* conn, ether_msg_t* msg) {
    uint8_t header_buf[ETHER_HEADER_SIZE];
    ether_msg_serialize_header(&msg->header, header_buf);

    // Send header
    if (send(conn->socket, header_buf, ETHER_HEADER_SIZE, 0) != ETHER_HEADER_SIZE) {
        return -1;
    }

    // Send payload if present
    if (msg->header.size > 0) {
        if (send(conn->socket, msg->payload, msg->header.size, 0) != (ssize_t)msg->header.size) {
            return -1;
        }
    }

    return 0;
}

/**
 * Receive a response from the server
 */
static int recv_response(ether_conn_t* conn, ether_msg_header_t* header,
                         void* payload_buf, size_t payload_max) {
    uint8_t header_buf[ETHER_HEADER_SIZE];

    // Receive header (blocking until complete)
    ssize_t n = recv(conn->socket, header_buf, ETHER_HEADER_SIZE, MSG_WAITALL);
    if (n != ETHER_HEADER_SIZE) {
        return -1;
    }

    ether_msg_deserialize_header(header_buf, header);

    // Validate response
    if (!ether_msg_validate(header)) {
        return -1;
    }

    // Receive payload if expected
    if (header->size > 0 && payload_buf) {
        size_t to_recv = (header->size < payload_max) ? header->size : payload_max;
        n = recv(conn->socket, payload_buf, to_recv, MSG_WAITALL);
        if (n != (ssize_t)to_recv) {
            return -1;
        }
    }

    return 0;
}

// =============================================================================
// PUBLIC API - CONNECTION
// =============================================================================

ether_conn_t* ether_connect(const char* host, int port) {
    ether_conn_t* conn = calloc(1, sizeof(ether_conn_t));
    if (!conn) return NULL;

    strncpy(conn->host, host, sizeof(conn->host) - 1);
    conn->port = port;

    // Resolve hostname to IP address
    struct hostent* he = gethostbyname(host);
    if (!he) {
        free(conn);
        return NULL;
    }

    // Create TCP socket
    conn->socket = socket(AF_INET, SOCK_STREAM, 0);
    if (conn->socket < 0) {
        free(conn);
        return NULL;
    }

    // Connect to server
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    memcpy(&addr.sin_addr, he->h_addr, he->h_length);

    if (connect(conn->socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(conn->socket);
        free(conn);
        return NULL;
    }

    conn->connected = 1;
    return conn;
}

void ether_disconnect(ether_conn_t* conn) {
    if (!conn) return;

    if (conn->connected) {
        close(conn->socket);
    }
    free(conn);
}

int ether_ping(ether_conn_t* conn) {
    if (!conn || !conn->connected) return -1;

    // Send PING
    ether_msg_t* msg = ether_msg_create(ETHER_CMD_PING, 0);
    if (!msg) return -1;

    if (send_message(conn, msg) != 0) {
        ether_msg_free(msg);
        return -1;
    }
    ether_msg_free(msg);

    // Wait for PONG
    ether_msg_header_t response;
    if (recv_response(conn, &response, NULL, 0) != 0) {
        return -1;
    }

    return (response.command == ETHER_CMD_PONG) ? 0 : -1;
}

// =============================================================================
// PUBLIC API - REMOTE MEMORY
// =============================================================================

void* ether_rmalloc(ether_conn_t* conn, size_t size) {
    if (!conn || !conn->connected || size == 0) return NULL;

    // 1. Create ALLOC request
    ether_msg_t* msg = ether_msg_create(ETHER_CMD_ALLOC, 0);
    if (!msg) return NULL;

    msg->header.size = size;  // Request size in header

    // 2. Send request
    if (send_message(conn, msg) != 0) {
        ether_msg_free(msg);
        return NULL;
    }
    ether_msg_free(msg);

    // 3. Receive response
    ether_msg_header_t response;
    if (recv_response(conn, &response, NULL, 0) != 0) {
        return NULL;
    }

    if (response.command != ETHER_CMD_OK) {
        return NULL;
    }

    // 4. Allocate local buffer for user convenience
    void* local_ptr = calloc(1, size);
    if (!local_ptr) {
        // TODO: Send FREE to server to clean up
        return NULL;
    }

    // 5. Cache the mapping: local_ptr -> remote_handle
    if (cache_store(local_ptr, response.handle, size) != 0) {
        free(local_ptr);
        // TODO: Send FREE to server
        return NULL;
    }

    return local_ptr;
}

void ether_rfree(ether_conn_t* conn, void* ptr) {
    if (!conn || !conn->connected || !ptr) return;

    // Look up remote handle
    uint64_t handle = cache_lookup(ptr, NULL);
    if (handle == 0) return;

    // Send FREE request
    ether_msg_t* msg = ether_msg_create(ETHER_CMD_FREE, 0);
    if (!msg) return;

    msg->header.handle = handle;

    send_message(conn, msg);
    ether_msg_free(msg);

    // Wait for confirmation (ignore errors)
    ether_msg_header_t response;
    recv_response(conn, &response, NULL, 0);

    // Clean up local resources
    cache_remove(ptr);
    free(ptr);
}

int ether_rwrite(ether_conn_t* conn, void* ptr, const void* data, size_t len) {
    if (!conn || !conn->connected || !ptr || !data) {
        return ETHER_ERR_INVALID;
    }

    // Look up remote handle
    size_t block_size;
    uint64_t handle = cache_lookup(ptr, &block_size);
    if (handle == 0) return ETHER_ERR_NOTFOUND;

    // Bounds check
    if (len > block_size) {
        return ETHER_ERR_OVERFLOW;
    }

    // Create WRITE request with payload
    ether_msg_t* msg = ether_msg_create(ETHER_CMD_WRITE, len);
    if (!msg) return ETHER_ERR_NOMEM;

    msg->header.handle = handle;
    memcpy(msg->payload, data, len);

    // Send request
    if (send_message(conn, msg) != 0) {
        ether_msg_free(msg);
        return ETHER_ERR_NETWORK;
    }
    ether_msg_free(msg);

    // Wait for confirmation
    ether_msg_header_t response;
    if (recv_response(conn, &response, NULL, 0) != 0) {
        return ETHER_ERR_NETWORK;
    }

    if (response.command != ETHER_CMD_OK) {
        return ETHER_ERR_INVALID;
    }

    // Update local cache with written data
    memcpy(ptr, data, len);

    return ETHER_OK;
}

int ether_rread(ether_conn_t* conn, void* ptr, void* buffer, size_t len) {
    if (!conn || !conn->connected || !ptr || !buffer) {
        return ETHER_ERR_INVALID;
    }

    // Look up remote handle
    size_t block_size;
    uint64_t handle = cache_lookup(ptr, &block_size);
    if (handle == 0) return ETHER_ERR_NOTFOUND;

    // Cap length to block size
    if (len > block_size) {
        len = block_size;
    }

    // Create READ request
    ether_msg_t* msg = ether_msg_create(ETHER_CMD_READ, 0);
    if (!msg) return ETHER_ERR_NOMEM;

    msg->header.handle = handle;
    msg->header.size = len;  // How many bytes to read

    // Send request
    if (send_message(conn, msg) != 0) {
        ether_msg_free(msg);
        return ETHER_ERR_NETWORK;
    }
    ether_msg_free(msg);

    // Receive response with data
    ether_msg_header_t response;
    if (recv_response(conn, &response, buffer, len) != 0) {
        return ETHER_ERR_NETWORK;
    }

    if (response.command != ETHER_CMD_OK) {
        return ETHER_ERR_INVALID;
    }

    return ETHER_OK;
}

size_t ether_rsize(ether_conn_t* conn, void* ptr) {
    if (!conn || !ptr) return 0;

    size_t size;
    if (cache_lookup(ptr, &size) == 0) {
        return 0;
    }
    return size;
}