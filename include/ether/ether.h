/**
 * Ether - Distributed Resource Allocation System
 *
 * Main public API header
 *
 * Copyright (C) 2024 - Licensed under GPL-3.0
 */

#ifndef ETHER_H
#define ETHER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// VERSION
// =============================================================================

#define ETHER_VERSION_MAJOR 0
#define ETHER_VERSION_MINOR 1
#define ETHER_VERSION_PATCH 0
#define ETHER_VERSION "0.1.0"

// =============================================================================
// ERROR CODES
// =============================================================================

typedef enum {
    ETHER_OK          =  0,   // Success
    ETHER_ERR_NOMEM   = -1,   // Out of memory
    ETHER_ERR_INVALID = -2,   // Invalid argument
    ETHER_ERR_CORRUPT = -3,   // Memory corruption detected
    ETHER_ERR_OVERFLOW= -4,   // Buffer overflow
    ETHER_ERR_NETWORK = -5,   // Network error
    ETHER_ERR_TIMEOUT = -6,   // Operation timeout
    ETHER_ERR_NOTFOUND= -7,   // Handle not found
} ether_error_t;

/**
 * Get human-readable error message
 *
 * @param err   Error code
 * @return      Static string describing the error
 */
const char* ether_strerror(ether_error_t err);

// =============================================================================
// LOCAL ALLOCATOR API
// =============================================================================

/**
 * Allocate a memory block
 *
 * Memory is zero-initialized for security.
 * Each block has a hidden header for metadata tracking.
 *
 * @param size  Size in bytes (must be > 0)
 * @return      Pointer to allocated memory, NULL on failure
 */
void* ether_alloc(size_t size);

/**
 * Free a memory block
 *
 * Performs secure wipe before freeing.
 * Safe to call with NULL pointer.
 *
 * @param ptr   Pointer to free (can be NULL)
 */
void ether_free(void* ptr);

/**
 * Reallocate a memory block
 *
 * - realloc(NULL, size) = alloc(size)
 * - realloc(ptr, 0) = free(ptr), returns NULL
 *
 * @param ptr       Existing pointer (can be NULL)
 * @param new_size  New size in bytes
 * @return          New pointer, NULL on failure (original unchanged)
 */
void* ether_realloc(void* ptr, size_t new_size);

/**
 * Write data to an allocated block
 *
 * @param ptr   Pointer to block
 * @param data  Source data
 * @param len   Bytes to write
 * @return      ETHER_OK or error code
 */
int ether_write(void* ptr, const void* data, size_t len);

/**
 * Read data from an allocated block
 *
 * @param ptr     Pointer to block
 * @param buffer  Destination buffer
 * @param len     Bytes to read
 * @return        ETHER_OK or error code
 */
int ether_read(void* ptr, void* buffer, size_t len);

/**
 * Get the size of an allocated block
 *
 * @param ptr   Pointer to block
 * @return      Size in bytes, 0 if invalid
 */
size_t ether_size(void* ptr);

// =============================================================================
// STATISTICS
// =============================================================================

typedef struct {
    size_t total_allocated;   // Total bytes ever allocated
    size_t total_freed;       // Total bytes ever freed
    size_t current_usage;     // Current bytes in use
    size_t peak_usage;        // Peak memory usage
    size_t num_allocs;        // Number of allocations
    size_t num_frees;         // Number of frees
} ether_stats_t;

/**
 * Get allocator statistics
 *
 * @return  Copy of current statistics
 */
ether_stats_t ether_get_stats(void);

/**
 * Reset statistics to zero
 */
void ether_reset_stats(void);

// =============================================================================
// DEBUG
// =============================================================================

/**
 * Enable/disable debug output
 *
 * @param enabled   true to enable, false to disable
 */
void ether_set_debug(bool enabled);

/**
 * Print internal state to stdout (for debugging)
 */
void ether_dump_state(void);

#ifdef __cplusplus
}
#endif

#endif // ETHER_H