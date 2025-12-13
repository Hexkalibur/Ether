/**
 * Simple Echo Server
 *
 * Basic TCP server example to learn socket programming.
 * Accepts connections and echoes back everything it receives.
 *
 * Usage:
 *   ./echo_server [port]
 *
 * Test with:
 *   nc localhost 9999
 *   echo "hello" | nc localhost 9999
 *
 * Copyright (C) 2024 - Licensed under GPL-3.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define DEFAULT_PORT 9999
#define BUFFER_SIZE  1024

static volatile int running = 1;

void signal_handler(int sig) {
    (void)sig;
    running = 0;
    printf("\nShutting down...\n");
}

int main(int argc, char** argv) {
    int port = DEFAULT_PORT;
    if (argc > 1) {
        port = atoi(argv[1]);
    }

    // Setup signal handler for graceful shutdown
    signal(SIGINT, signal_handler);

    // 1. Create socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    // 2. Set socket options (allow address reuse)
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 3. Bind to address
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;  // Listen on all interfaces
    addr.sin_port = htons(port);         // Convert to network byte order

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }

    // 4. Start listening for connections
    if (listen(server_fd, 10) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    printf("Echo server listening on port %d\n", port);
    printf("Press Ctrl+C to stop\n\n");

    // 5. Accept loop
    while (running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        printf("Waiting for connection...\n");
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);

        if (client_fd < 0) {
            if (!running) break;
            perror("accept");
            continue;
        }

        // Print client info
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        printf("Client connected: %s:%d\n", client_ip, ntohs(client_addr.sin_port));

        // 6. Echo loop - read and send back
        char buffer[BUFFER_SIZE];
        ssize_t bytes_read;

        while ((bytes_read = read(client_fd, buffer, sizeof(buffer) - 1)) > 0) {
            buffer[bytes_read] = '\0';
            printf("  Received: %s", buffer);

            // Echo back to client
            write(client_fd, buffer, bytes_read);
        }

        printf("Client disconnected\n\n");
        close(client_fd);
    }

    close(server_fd);
    printf("Server stopped\n");

    return 0;
}