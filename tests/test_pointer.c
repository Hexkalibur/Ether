/**
* Test: Understanding pointer arithmetic in Ether
*/

#include "ether/ether.h"
#include <stdio.h>
#include <stdint.h>

// The header size (same as in allocator.c)
#define HEADER_SIZE 24  // sizeof(block_header_t)

void test_pointers() {
    void* ptr = ether_alloc(100);

    printf("User pointer:     %p\n", ptr);
    printf("Header should be: %p (ptr - %d bytes)\n",
           (void*)((char*)ptr - HEADER_SIZE), HEADER_SIZE);
    printf("Block size:       %zu bytes\n", ether_size(ptr));

    // Verify the magic number is there (peek at memory before ptr)
    uint32_t* magic_location = (uint32_t*)((char*)ptr - HEADER_SIZE);
    printf("Magic number:     0x%X (should be 0xF9A9582B)\n", *magic_location);
    ether_free(ptr);
}
