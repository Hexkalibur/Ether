/**
 * Ether Daemon (etherd) - Server MVP
 *
 * Per ora: semplice TCP server che accetta connessioni
 * e risponde ai comandi del protocollo Ether.
 *
 * TODO: Migrare a libuv per async I/O
 */

#include "ether/ether.h"
#include "ether/protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// =============================================================================
// CONFIGURATION
// =============================================================================

#define MAX_CLIENTS     64
#define BUFFER_SIZE     4096

// =============================================================================
// HANDLE MAPPING
// =============================================================================

// Semplice mapping handle → pointer (per MVP)
typedef struct {
    uint64_t handle;
    void *ptr;
    size_t size;
    int in_use;
} handle_entry_t;

static handle_entry_t g_handles[1024];
static uint64_t g_next_handle = 1;

static uint64_t store_handle(void *ptr, size_t size) {
    for (int i = 0; i < 1024; i++) {
        if (!g_handles[i].in_use) {
            g_handles[i].handle = g_next_handle++;
            g_handles[i].ptr = ptr;
            g_handles[i].size = size;
            g_handles[i].in_use = 1;
            return g_handles[i].handle;
        }
    }
    return 0; // No space
}

static void *lookup_handle(uint64_t handle, size_t *size) {
    for (int i = 0; i < 1024; i++) {
        if (g_handles[i].in_use && g_handles[i].handle == handle) {
            if (size) *size = g_handles[i].size;
            return g_handles[i].ptr;
        }
    }
    return NULL;
}

static int remove_handle(uint64_t handle) {
    for (int i = 0; i < 1024; i++) {
        if (g_handles[i].in_use && g_handles[i].handle == handle) {
            g_handles[i].in_use = 0;
            g_handles[i].ptr = NULL;
            return 1;
        }
    }
    return 0;
}

// =============================================================================
// GLOBAL STATE
// =============================================================================

static volatile int g_running = 1;
static int g_server_fd = -1;

static void signal_handler(int sig) {
    (void) sig;
    printf("\n[etherd] Shutting down...\n");
    g_running = 0;
}

// =============================================================================
// REQUEST HANDLERS
// =============================================================================

static void send_response(int client_fd, ether_cmd_t cmd, uint64_t handle,
                          const void *data, size_t data_len) {
    ether_msg_t *response = ether_msg_create(cmd, data_len);
    if (!response) return;

    response->header.handle = handle;
    if (data && data_len > 0) {
        memcpy(response->payload, data, data_len);
    }

    // Serialize header
    uint8_t header_buf[ETHER_HEADER_SIZE];
    ether_msg_serialize_header(&response->header, header_buf);

    // Send header + payload
    send(client_fd, header_buf, ETHER_HEADER_SIZE, 0);
    if (data_len > 0) {
        send(client_fd, response->payload, data_len, 0);
    }

    ether_msg_free(response);
}

static void handle_ping(int client_fd) {
    printf("[etherd] PING received\n");
    send_response(client_fd, ETHER_CMD_PONG, 0, NULL, 0);
}

static void handle_alloc(int client_fd, ether_msg_header_t *header) {
    size_t size = header->size; // Size richiesta nel campo size

    printf("[etherd] ALLOC request: %zu bytes\n", size);

    void *ptr = ether_alloc(size);
    if (!ptr) {
        printf("[etherd] ALLOC failed!\n");
        send_response(client_fd, ETHER_CMD_ERROR, 0, NULL, 0);
        return;
    }

    uint64_t handle = store_handle(ptr, size);
    if (handle == 0) {
        ether_free(ptr);
        send_response(client_fd, ETHER_CMD_ERROR, 0, NULL, 0);
        return;
    }

    printf("[etherd] ALLOC OK: handle=0x%lX ptr=%p\n", (unsigned long) handle, ptr);
    send_response(client_fd, ETHER_CMD_OK, handle, NULL, 0);
}

static void handle_free(int client_fd, ether_msg_header_t *header) {
    uint64_t handle = header->handle;

    printf("[etherd] FREE request: handle=0x%lX\n", (unsigned long) handle);

    void *ptr = lookup_handle(handle, NULL);
    if (!ptr) {
        printf("[etherd] FREE failed: handle not found\n");
        send_response(client_fd, ETHER_CMD_ERROR, handle, NULL, 0);
        return;
    }

    ether_free(ptr);
    remove_handle(handle);

    printf("[etherd] FREE OK\n");
    send_response(client_fd, ETHER_CMD_OK, handle, NULL, 0);
}

static void handle_write(int client_fd, ether_msg_header_t *header,
                         const uint8_t *payload) {
    uint64_t handle = header->handle;
    size_t len = header->size;

    printf("[etherd] WRITE request: handle=0x%lX len=%zu\n",
           (unsigned long) handle, len);

    size_t block_size;
    void *ptr = lookup_handle(handle, &block_size);
    if (!ptr) {
        send_response(client_fd, ETHER_CMD_ERROR, handle, NULL, 0);
        return;
    }

    if (len > block_size) {
        printf("[etherd] WRITE failed: overflow\n");
        send_response(client_fd, ETHER_CMD_ERROR, handle, NULL, 0);
        return;
    }

    int ret = ether_write(ptr, payload, len);
    if (ret != ETHER_OK) {
        send_response(client_fd, ETHER_CMD_ERROR, handle, NULL, 0);
        return;
    }

    printf("[etherd] WRITE OK\n");
    send_response(client_fd, ETHER_CMD_OK, handle, NULL, 0);
}

static void handle_read(int client_fd, ether_msg_header_t *header) {
    uint64_t handle = header->handle;
    size_t len = header->size; // Quanti bytes leggere

    printf("[etherd] READ request: handle=0x%lX len=%zu\n",
           (unsigned long) handle, len);

    size_t block_size;
    void *ptr = lookup_handle(handle, &block_size);
    if (!ptr) {
        send_response(client_fd, ETHER_CMD_ERROR, handle, NULL, 0);
        return;
    }

    if (len > block_size) {
        len = block_size; // Cap to available
    }

    uint8_t *buffer = malloc(len);
    if (!buffer) {
        send_response(client_fd, ETHER_CMD_ERROR, handle, NULL, 0);
        return;
    }

    int ret = ether_read(ptr, buffer, len);
    if (ret != ETHER_OK) {
        free(buffer);
        send_response(client_fd, ETHER_CMD_ERROR, handle, NULL, 0);
        return;
    }

    printf("[etherd] READ OK: sending %zu bytes\n", len);
    send_response(client_fd, ETHER_CMD_OK, handle, buffer, len);
    free(buffer);
}

// =============================================================================
// CLIENT HANDLING
// =============================================================================

static void handle_client(int client_fd) {
    uint8_t header_buf[ETHER_HEADER_SIZE];
    uint8_t *payload = NULL;

    while (g_running) {
        // 1. Read header
        ssize_t n = recv(client_fd, header_buf, ETHER_HEADER_SIZE, MSG_WAITALL);
        if (n <= 0) {
            if (n < 0) perror("[etherd] recv header");
            break;
        }

        // 2. Deserialize header
        ether_msg_header_t header;
        ether_msg_deserialize_header(header_buf, &header);

        // 3. Validate
        if (!ether_msg_validate(&header)) {
            printf("[etherd] Invalid message received\n");
            continue;
        }

        // 4. Read payload if any
        if (header.size > 0) {
            payload = malloc(header.size);
            if (!payload) {
                printf("[etherd] Out of memory for payload\n");
                break;
            }

            n = recv(client_fd, payload, header.size, MSG_WAITALL);
            if (n <= 0) {
                free(payload);
                break;
            }
        }

        // 5. Handle command
        switch (header.command) {
            case ETHER_CMD_PING:
                handle_ping(client_fd);
                break;
            case ETHER_CMD_ALLOC:
                handle_alloc(client_fd, &header);
                break;
            case ETHER_CMD_FREE:
                handle_free(client_fd, &header);
                break;
            case ETHER_CMD_WRITE:
                handle_write(client_fd, &header, payload);
                break;
            case ETHER_CMD_READ:
                handle_read(client_fd, &header);
                break;
            default:
                printf("[etherd] Unknown command: 0x%02X\n", header.command);
                send_response(client_fd, ETHER_CMD_ERROR, 0, NULL, 0);
        }

        if (payload) {
            free(payload);
            payload = NULL;
        }
    }

    close(client_fd);
    printf("[etherd] Client disconnected\n");
}

// =============================================================================
// MAIN
// =============================================================================

int main(int argc, char **argv) {
    int port = ETHER_DEFAULT_PORT;

    // Parse arguments
    if (argc > 1) {
        port = atoi(argv[1]);
    }

    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Create socket
    g_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_server_fd < 0) {
        perror("socket");
        return 1;
    }

    // Allow reuse
    int opt = 1;
    setsockopt(g_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Bind
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(g_server_fd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        perror("bind");
        close(g_server_fd);
        return 1;
    }

    // Listen
    if (listen(g_server_fd, MAX_CLIENTS) < 0) {
        perror("listen");
        close(g_server_fd);
        return 1;
    }

    printf("===========================================\n");
    printf("  Ether Daemon v%s\n", ETHER_VERSION);
    printf("  Privacy-First Memory-as-a-Service\n");
    printf("===========================================\n");
    printf("Listening on 0.0.0.0:%d\n", port);
    printf("Press Ctrl+C to stop\n\n");

    // Accept loop
    while (g_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(g_server_fd, (struct sockaddr *) &client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue; // Signal interrupted
            perror("accept");
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        printf("[etherd] Client connected from %s:%d\n",
               client_ip, ntohs(client_addr.sin_port));

        // Handle client (blocking, single-threaded per ora)
        // TODO: fork() o threads per multi-client
        handle_client(client_fd);
    }

    // Cleanup
    close(g_server_fd);
    ether_dump_state();

    printf("[etherd] Goodbye!\n");
    return 0;
}