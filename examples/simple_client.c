/**
 * Simple Client Example
 *
 * Demonstrates basic usage of rmalloc, rwrite, rread, rfree.
 *
 * Usage:
 *   1. Start the server: ./etherd
 *   2. Run this client:  ./simple_client [host] [port]
 *
 * Copyright (C) 2024 - Licensed under GPL-3.0
 */

#include "ether/client.h"
#include "ether/ether.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv) {
    const char* host = "localhost";
    int port = 9999;

    if (argc > 1) host = argv[1];
    if (argc > 2) port = atoi(argv[2]);

    printf("===========================================\n");
    printf("  Ether Client Example\n");
    printf("===========================================\n\n");

    // 1. Connect to server
    printf("[1] Connecting to %s:%d...\n", host, port);
    ether_conn_t* conn = ether_connect(host, port);
    if (!conn) {
        printf("    FAILED! Make sure etherd is running.\n");
        return 1;
    }
    printf("    Connected!\n\n");

    // 2. Ping server
    printf("[2] Sending PING...\n");
    if (ether_ping(conn) == 0) {
        printf("    PONG received! Server is alive.\n\n");
    } else {
        printf("    PING failed!\n");
        ether_disconnect(conn);
        return 1;
    }

    // 3. Allocate remote memory
    printf("[3] Allocating 256 bytes of remote memory...\n");
    void* ptr = ether_rmalloc(conn, 256);
    if (!ptr) {
        printf("    rmalloc failed!\n");
        ether_disconnect(conn);
        return 1;
    }
    printf("    Got handle: %p\n\n", ptr);

    // 4. Write data
    const char* secret = "Hello from remote memory!";
    printf("[4] Writing: \"%s\"\n", secret);

    int ret = ether_rwrite(conn, ptr, secret, strlen(secret) + 1);
    if (ret != ETHER_OK) {
        printf("    rwrite failed: %s\n", ether_strerror(ret));
    } else {
        printf("    Write OK!\n\n");
    }

    // 5. Read data back
    printf("[5] Reading back...\n");
    char buffer[256] = {0};
    ret = ether_rread(conn, ptr, buffer, 256);
    if (ret != ETHER_OK) {
        printf("    rread failed: %s\n", ether_strerror(ret));
    } else {
        printf("    Read: \"%s\"\n\n", buffer);
    }

    // 6. Verify data integrity
    printf("[6] Verifying data integrity...\n");
    if (strcmp(secret, buffer) == 0) {
        printf("    ✓ Data matches! Remote memory works correctly.\n\n");
    } else {
        printf("    ✗ Data mismatch!\n\n");
    }

    // 7. Free remote memory
    printf("[7] Freeing remote memory...\n");
    ether_rfree(conn, ptr);
    printf("    Free OK!\n\n");

    // 8. Disconnect
    printf("[8] Disconnecting...\n");
    ether_disconnect(conn);
    printf("    Done!\n");

    printf("\n===========================================\n");
    printf("  Example completed successfully!\n");
    printf("===========================================\n");

    return 0;
}