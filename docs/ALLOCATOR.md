# Memory Allocator

[Back to README](../README.md) | [Architecture](ARCHITECTURE.md)

This document explains how the Ether memory allocator works, including the header structure, pointer arithmetic, and security features.

---

## Overview

The Ether allocator is a wrapper around malloc/free that adds:

1. **Metadata Tracking** - Size, flags, and validation in hidden headers
2. **Corruption Detection** - Magic numbers to detect invalid operations
3. **Secure Wiping** - Zero memory before freeing to prevent data leakage
4. **Statistics** - Track allocations, frees, and memory usage

---

## Header Structure

Every allocation has a hidden header that precedes the user data:

```c
typedef struct block_header {
    uint32_t magic;      // 4 bytes - Validation signature
    uint32_t flags;      // 4 bytes - Status flags
    size_t   size;       // 8 bytes - User-requested size
    size_t   capacity;   // 8 bytes - Actual capacity
} block_header_t;        // Total: 24 bytes
```

### Magic Numbers

Two magic numbers identify block state:

```c
#define BLOCK_MAGIC  0xF9A9582B  // CRC32("YETHERED") - Block is allocated
#define BLOCK_FREED  0x8FD76019  // CRC32("NETHERED") - Block was freed
```

These are CRC32 hashes of meaningful strings, chosen because:
- They are unlikely to appear randomly in memory
- They are reproducible (not arbitrary hex values)
- They have meaning ("Yet Ethered" / "Not Ethered")

### Flags

```c
#define FLAG_ALLOCATED  0x01  // Block is currently allocated
#define FLAG_ENCRYPTED  0x02  // Reserved for future use
```

---

## Memory Layout

When you call `ether_alloc(100)`:

```
Address:    0x1000                    0x1018                    0x107C
            │                         │                         │
            ▼                         ▼                         ▼
            ┌─────────────────────────┬─────────────────────────┐
            │    HEADER (24 bytes)    │   USER DATA (100 bytes) │
            ├─────────────────────────┼─────────────────────────┤
            │ magic:    0xF9A9582B    │                         │
            │ flags:    0x00000001    │ ← Pointer returned      │
            │ size:     100           │   to user (0x1018)      │
            │ capacity: 100           │                         │
            └─────────────────────────┴─────────────────────────┘
            │                         │                         │
            │◄─────── 24 bytes ───────►◄────── 100 bytes ──────►│
            │                         │                         │
            │◄──────────── malloc(124) ────────────────────────►│
```

The user receives pointer `0x1018`, unaware that the header exists at `0x1000`.

---

## Pointer Arithmetic

### The Key Insight

When you have a pointer to a structure type, adding or subtracting 1 moves by the SIZE of the structure, not by 1 byte.

```c
block_header_t* header = (block_header_t*)0x1000;

header + 1;  // = 0x1018 (moves forward 24 bytes)
header - 1;  // = 0x0FE8 (moves backward 24 bytes)
```

### Getting the Header from User Pointer

```c
static inline block_header_t* get_header(void* ptr) {
    return ((block_header_t*)ptr) - 1;
}
```

Step by step:
```
ptr = 0x1018 (user pointer)

1. Cast to block_header_t*:
   (block_header_t*)0x1018

2. Subtract 1 (moves back by sizeof(block_header_t) = 24 bytes):
   0x1018 - 24 = 0x1000

3. Result: header is at 0x1000
```

### Getting User Pointer from Header

```c
static inline void* get_user_ptr(block_header_t* header) {
    return (void*)(header + 1);
}
```

Step by step:
```
header = 0x1000

1. Add 1 (moves forward by sizeof(block_header_t) = 24 bytes):
   0x1000 + 24 = 0x1018

2. Cast to void*:
   (void*)0x1018

3. Result: user pointer is 0x1018
```

---

## Allocation Flow

```c
void* ether_alloc(size_t size) {
    // 1. Validate size
    if (size == 0) return NULL;

    // 2. Calculate total allocation
    size_t total = HEADER_SIZE + size;  // 24 + 100 = 124

    // 3. Allocate from system
    block_header_t* header = malloc(total);
    if (!header) return NULL;

    // 4. Initialize header
    header->magic = BLOCK_MAGIC;     // 0xF9A9582B
    header->flags = FLAG_ALLOCATED;  // 0x01
    header->size = size;             // 100
    header->capacity = size;         // 100

    // 5. Get user pointer
    void* user_ptr = get_user_ptr(header);  // header + 1

    // 6. Zero user memory (security)
    memset(user_ptr, 0, size);

    // 7. Update statistics
    g_stats.total_allocated += size;
    g_stats.current_usage += size;
    g_stats.num_allocs++;

    // 8. Return user pointer
    return user_ptr;
}
```

---

## Free Flow

```c
void ether_free(void* ptr) {
    // 1. Handle NULL
    if (!ptr) return;

    // 2. Get header
    block_header_t* header = get_header(ptr);  // ptr - 1

    // 3. Validate magic number
    if (header->magic != BLOCK_MAGIC) {
        // CORRUPTION! Someone passed us an invalid pointer
        fprintf(stderr, "ERROR: invalid free\n");
        return;
    }

    // 4. Check for double-free
    if (header->magic == BLOCK_FREED) {
        fprintf(stderr, "ERROR: double free\n");
        return;
    }

    // 5. Secure wipe: zero user data
    memset(ptr, 0, header->size);

    // 6. Mark as freed
    header->magic = BLOCK_FREED;  // 0x8FD76019
    header->flags = 0;

    // 7. Update statistics
    g_stats.total_freed += header->size;
    g_stats.current_usage -= header->size;
    g_stats.num_frees++;

    // 8. Return to system (MUST pass original malloc address!)
    free(header);  // NOT free(ptr)!
}
```

### Critical Point: free(header), not free(ptr)

```
WRONG: free(ptr)
       ptr = 0x1018
       malloc() never returned this address!
       CRASH or heap corruption

RIGHT: free(header)
       header = 0x1000
       This is what malloc() returned
```

---

## Validation

The `is_valid_block()` function checks if a pointer is valid:

```c
static inline int is_valid_block(block_header_t* header) {
    return header &&                           // Not NULL
           header->magic == BLOCK_MAGIC &&     // Has correct magic
           (header->flags & FLAG_ALLOCATED);   // Is allocated
}
```

This catches:
- NULL pointers
- Random/garbage pointers (wrong magic)
- Already-freed blocks (magic changed to BLOCK_FREED)
- Stack/global variables passed as heap pointers

---

## Realloc Flow

```c
void* ether_realloc(void* ptr, size_t new_size) {
    // Handle special cases
    if (!ptr) return ether_alloc(new_size);
    if (new_size == 0) { ether_free(ptr); return NULL; }

    block_header_t* header = get_header(ptr);
    
    // Validate
    if (!is_valid_block(header)) return NULL;

    // If new size fits in existing capacity, reuse
    if (new_size <= header->capacity) {
        header->size = new_size;
        return ptr;
    }

    // Otherwise, allocate new block
    void* new_ptr = ether_alloc(new_size);
    if (!new_ptr) return NULL;

    // Copy data
    memcpy(new_ptr, ptr, header->size);

    // Free old block
    ether_free(ptr);

    return new_ptr;
}
```

---

## Statistics

The allocator tracks usage:

```c
typedef struct {
    size_t total_allocated;   // Bytes ever allocated
    size_t total_freed;       // Bytes ever freed
    size_t current_usage;     // Bytes currently in use
    size_t peak_usage;        // Maximum bytes ever in use
    size_t num_allocs;        // Number of alloc calls
    size_t num_frees;         // Number of free calls
} ether_stats_t;
```

Example output:
```
=== Ether Allocator State ===
Total allocated: 1048576 bytes
Total freed:     1048576 bytes
Current usage:   0 bytes
Peak usage:      524288 bytes
Allocations:     1000
Frees:           1000
=============================
```

---

## Security Features

### 1. Zero on Allocate

All memory is zeroed before returning to user:

```c
memset(user_ptr, 0, size);
```

This prevents information leakage from previous allocations.

### 2. Secure Wipe on Free

Memory is zeroed before being freed:

```c
memset(ptr, 0, header->size);
```

This prevents:
- Reading sensitive data from freed memory
- Data remaining in memory after program exits

### 3. Corruption Detection

Magic numbers detect invalid operations:

```c
if (header->magic != BLOCK_MAGIC) {
    // Invalid pointer or corruption
}
if (header->magic == BLOCK_FREED) {
    // Double-free attempt
}
```

---

## Limitations

1. **Not Thread-Safe** - Global statistics without mutex protection
2. **No Pool Allocation** - Each alloc/free calls malloc/free
3. **Simple Validation** - Magic numbers can theoretically collide

These are intentional simplifications for the MVP.

---

## API Reference

```c
// Allocate size bytes, returns NULL on failure
void* ether_alloc(size_t size);

// Free a block, safe to call with NULL
void ether_free(void* ptr);

// Reallocate to new_size, returns NULL on failure
void* ether_realloc(void* ptr, size_t new_size);

// Write data to block, returns error code
int ether_write(void* ptr, const void* data, size_t len);

// Read data from block, returns error code
int ether_read(void* ptr, void* buffer, size_t len);

// Get block size, returns 0 if invalid
size_t ether_size(void* ptr);

// Get current statistics
ether_stats_t ether_get_stats(void);

// Reset statistics to zero
void ether_reset_stats(void);

// Enable/disable debug output
void ether_set_debug(bool enabled);

// Print internal state
void ether_dump_state(void);

// Get error message string
const char* ether_strerror(ether_error_t err);
```

---

## Next Steps

The allocator is complete for Phase 1. Future enhancements:
- Thread-safe version with mutex protection
- Pool allocator for reduced syscall overhead
- Memory alignment options
