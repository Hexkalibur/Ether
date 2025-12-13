/**
 * Ether Memory Allocator
 *
 * Allocator with hidden headers for metadata tracking.
 *
 * Memory Layout:
 *   [block_header_t][user_data...]
 *                   ^-- pointer returned to user
 *
 * Copyright (C) 2024 - Licensed under GPL-3.0
 */

#include "ether/ether.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

// =============================================================================
// INTERNAL STRUCTURES
// =============================================================================

#define BLOCK_MAGIC  0xF9A9582B  // CRC32("YETHERED") - block is allocated
#define BLOCK_FREED  0x8FD76019  // CRC32("NETHERED") - block was freed

// Block flags
#define FLAG_ALLOCATED  0x01
#define FLAG_ENCRYPTED  0x02    // Reserved for future encryption support

/**
 * Header preceding each allocated block in memory.
 * Hidden from the user - they only see the data area.
 */
typedef struct block_header {
    uint32_t magic;      // Sanity check (BLOCK_MAGIC or BLOCK_FREED)
    uint32_t flags;      // Block status flags
    size_t   size;       // User-requested size
    size_t   capacity;   // Actual capacity (may be larger for realloc)
} block_header_t;

#define HEADER_SIZE sizeof(block_header_t)

// =============================================================================
// GLOBAL STATE
// =============================================================================

static ether_stats_t g_stats = {0};
static bool g_debug = false;

// =============================================================================
// HELPER FUNCTIONS
// =============================================================================

/**
 * Given a user pointer, get the hidden header
 *
 * @param ptr  User pointer (points to data area)
 * @return     Pointer to header (24 bytes before user pointer)
 */
static inline block_header_t* get_header(void* ptr) {
    return ((block_header_t*)ptr) - 1;
}

/**
 * Given a header, get the user pointer
 *
 * @param header  Pointer to header
 * @return        User pointer (data area)
 */
static inline void* get_user_ptr(block_header_t* header) {
    return (void*)(header + 1);
}

/**
 * Validate that a block is valid and allocated
 *
 * @param header  Header to check
 * @return        1 if valid, 0 if invalid
 */
static inline int is_valid_block(block_header_t* header) {
    return header &&
           header->magic == BLOCK_MAGIC &&
           (header->flags & FLAG_ALLOCATED);
}

/**
 * Debug print helper
 */
static void debug_print(const char* fmt, ...) {
    if (!g_debug) return;

    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "[ETHER] ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

// =============================================================================
// ERROR MESSAGES
// =============================================================================

const char* ether_strerror(ether_error_t err) {
    switch (err) {
        case ETHER_OK:          return "Success";
        case ETHER_ERR_NOMEM:   return "Out of memory";
        case ETHER_ERR_INVALID: return "Invalid argument";
        case ETHER_ERR_CORRUPT: return "Memory corruption detected";
        case ETHER_ERR_OVERFLOW:return "Buffer overflow";
        case ETHER_ERR_NETWORK: return "Network error";
        case ETHER_ERR_TIMEOUT: return "Operation timeout";
        case ETHER_ERR_NOTFOUND:return "Handle not found";
        default:                return "Unknown error";
    }
}

// =============================================================================
// PUBLIC API - ALLOCATION
// =============================================================================

void* ether_alloc(size_t size) {
    if (size == 0) {
        return NULL;
    }

    // Allocate header + user data
    size_t total = HEADER_SIZE + size;
    block_header_t* header = (block_header_t*)malloc(total);

    if (!header) {
        debug_print("alloc FAILED: size=%zu", size);
        return NULL;
    }

    // Initialize header
    header->magic = BLOCK_MAGIC;
    header->flags = FLAG_ALLOCATED;
    header->size = size;
    header->capacity = size;

    // Zero user memory (security best practice)
    void* user_ptr = get_user_ptr(header);
    memset(user_ptr, 0, size);

    // Update statistics
    g_stats.total_allocated += size;
    g_stats.current_usage += size;
    g_stats.num_allocs++;
    if (g_stats.current_usage > g_stats.peak_usage) {
        g_stats.peak_usage = g_stats.current_usage;
    }

    debug_print("alloc OK: ptr=%p size=%zu", user_ptr, size);
    return user_ptr;
}

void ether_free(void* ptr) {
    if (!ptr) {
        return;  // free(NULL) is always safe
    }

    block_header_t* header = get_header(ptr);

    // Validate block
    if (!is_valid_block(header)) {
        fprintf(stderr, "[ETHER] ERROR: invalid free at %p (magic=0x%X)\n",
                ptr, header->magic);
        return;  // Don't abort, but report error
    }

    // Detect double-free
    if (header->magic == BLOCK_FREED) {
        fprintf(stderr, "[ETHER] ERROR: double free at %p\n", ptr);
        return;
    }

    size_t size = header->size;

    // Secure wipe: zero data before freeing (prevents data leakage)
    memset(ptr, 0, size);

    // Mark as freed (helps detect double-free and use-after-free in debug)
    header->magic = BLOCK_FREED;
    header->flags = 0;

    // Update statistics
    g_stats.total_freed += size;
    g_stats.current_usage -= size;
    g_stats.num_frees++;

    debug_print("free OK: ptr=%p size=%zu", ptr, size);

    // Return memory to system
    free(header);
}

void* ether_realloc(void* ptr, size_t new_size) {
    // realloc(NULL, size) = malloc(size)
    if (!ptr) {
        return ether_alloc(new_size);
    }

    // realloc(ptr, 0) = free(ptr)
    if (new_size == 0) {
        ether_free(ptr);
        return NULL;
    }

    block_header_t* old_header = get_header(ptr);

    if (!is_valid_block(old_header)) {
        fprintf(stderr, "[ETHER] ERROR: invalid realloc at %p\n", ptr);
        return NULL;
    }

    size_t old_size = old_header->size;

    // If new size fits in existing capacity, reuse block
    if (new_size <= old_header->capacity) {
        old_header->size = new_size;

        // Zero any newly exposed memory
        if (new_size > old_size) {
            memset((uint8_t*)ptr + old_size, 0, new_size - old_size);
        }
        return ptr;
    }

    // Otherwise, allocate new block
    void* new_ptr = ether_alloc(new_size);
    if (!new_ptr) {
        return NULL;  // Original block unchanged
    }

    // Copy existing data
    size_t copy_size = (old_size < new_size) ? old_size : new_size;
    memcpy(new_ptr, ptr, copy_size);

    // Free old block
    ether_free(ptr);

    debug_print("realloc OK: old=%p new=%p old_size=%zu new_size=%zu",
                ptr, new_ptr, old_size, new_size);

    return new_ptr;
}

// =============================================================================
// PUBLIC API - DATA OPERATIONS
// =============================================================================

int ether_write(void* ptr, const void* data, size_t len) {
    if (!ptr || !data) {
        return ETHER_ERR_INVALID;
    }

    block_header_t* header = get_header(ptr);

    if (!is_valid_block(header)) {
        return ETHER_ERR_CORRUPT;
    }

    if (len > header->size) {
        return ETHER_ERR_OVERFLOW;
    }

    memcpy(ptr, data, len);

    debug_print("write OK: ptr=%p len=%zu", ptr, len);
    return ETHER_OK;
}

int ether_read(void* ptr, void* buffer, size_t len) {
    if (!ptr || !buffer) {
        return ETHER_ERR_INVALID;
    }

    block_header_t* header = get_header(ptr);

    if (!is_valid_block(header)) {
        return ETHER_ERR_CORRUPT;
    }

    if (len > header->size) {
        return ETHER_ERR_OVERFLOW;
    }

    memcpy(buffer, ptr, len);

    debug_print("read OK: ptr=%p len=%zu", ptr, len);
    return ETHER_OK;
}

size_t ether_size(void* ptr) {
    if (!ptr) {
        return 0;
    }

    block_header_t* header = get_header(ptr);

    if (!is_valid_block(header)) {
        return 0;
    }

    return header->size;
}

// =============================================================================
// STATISTICS
// =============================================================================

ether_stats_t ether_get_stats(void) {
    return g_stats;
}

void ether_reset_stats(void) {
    memset(&g_stats, 0, sizeof(g_stats));
}

// =============================================================================
// DEBUG
// =============================================================================

void ether_set_debug(bool enabled) {
    g_debug = enabled;
}

void ether_dump_state(void) {
    printf("=== Ether Allocator State ===\n");
    printf("Total allocated: %zu bytes\n", g_stats.total_allocated);
    printf("Total freed:     %zu bytes\n", g_stats.total_freed);
    printf("Current usage:   %zu bytes\n", g_stats.current_usage);
    printf("Peak usage:      %zu bytes\n", g_stats.peak_usage);
    printf("Allocations:     %zu\n", g_stats.num_allocs);
    printf("Frees:           %zu\n", g_stats.num_frees);
    printf("=============================\n");
}