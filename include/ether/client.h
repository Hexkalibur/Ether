/**
 * Ether Client Library
 *
 * Remote memory operations: rmalloc, rfree, rwrite, rread
 *
 * Copyright (C) 2024 - Licensed under GPL-3.0
 */

#ifndef ETHER_CLIENT_H
#define ETHER_CLIENT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// CONNECTION
// =============================================================================

/**
 * Opaque connection handle
 */
typedef struct ether_conn ether_conn_t;

/**
 * Connect to an Ether server
 *
 * @param host  Hostname or IP address (e.g., "localhost", "192.168.1.1")
 * @param port  Port number (default: 9999)
 * @return      Connection handle, NULL on failure
 */
ether_conn_t* ether_connect(const char* host, int port);

/**
 * Disconnect and free resources
 *
 * @param conn  Connection to close (can be NULL)
 */
void ether_disconnect(ether_conn_t* conn);

/**
 * Check if connection is alive
 *
 * @param conn  Connection handle
 * @return      0 if OK, -1 on error
 */
int ether_ping(ether_conn_t* conn);

// =============================================================================
// REMOTE MEMORY API
// =============================================================================

/**
 * Allocate remote memory (rmalloc)
 *
 * Returns a local pointer that represents the remote memory.
 * The actual data lives on the server; this is just a handle.
 *
 * @param conn  Active connection
 * @param size  Bytes to allocate
 * @return      Handle (local pointer), NULL on failure
 */
void* ether_rmalloc(ether_conn_t* conn, size_t size);

/**
 * Free remote memory (rfree)
 *
 * @param conn  Active connection
 * @param ptr   Handle returned by ether_rmalloc()
 */
void ether_rfree(ether_conn_t* conn, void* ptr);

/**
 * Write data to remote memory (rwrite)
 *
 * @param conn  Active connection
 * @param ptr   Memory handle
 * @param data  Data to write
 * @param len   Data length in bytes
 * @return      ETHER_OK or error code
 */
int ether_rwrite(ether_conn_t* conn, void* ptr, const void* data, size_t len);

/**
 * Read data from remote memory (rread)
 *
 * @param conn    Active connection
 * @param ptr     Memory handle
 * @param buffer  Destination buffer
 * @param len     Bytes to read
 * @return        ETHER_OK or error code
 */
int ether_rread(ether_conn_t* conn, void* ptr, void* buffer, size_t len);

/**
 * Get size of remote memory block
 *
 * @param conn  Active connection
 * @param ptr   Memory handle
 * @return      Size in bytes, 0 on error
 */
size_t ether_rsize(ether_conn_t* conn, void* ptr);

#ifdef __cplusplus
}
#endif

#endif // ETHER_CLIENT_H