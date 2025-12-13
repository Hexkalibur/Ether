/**
 * Ether Allocator Test Suite
 *
 * Comprehensive tests for the memory allocator.
 *
 * Copyright (C) 2024 - Licensed under GPL-3.0
 */

#include "ether/ether.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// =============================================================================
// TEST UTILITIES
// =============================================================================

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { \
        printf("  Testing: %-30s ", #name); \
        fflush(stdout); \
        tests_run++; \
        name(); \
        tests_passed++; \
        printf("✓ PASSED\n"); \
    } while(0)

#define ASSERT(cond) \
    do { \
        if (!(cond)) { \
            printf("✗ FAILED\n"); \
            printf("    Assertion failed: %s\n", #cond); \
            printf("    At %s:%d\n", __FILE__, __LINE__); \
            exit(1); \
        } \
    } while(0)

// =============================================================================
// TESTS
// =============================================================================

void test_basic_alloc_free(void) {
    void* ptr = ether_alloc(100);
    ASSERT(ptr != NULL);

    size_t size = ether_size(ptr);
    ASSERT(size == 100);

    ether_free(ptr);
}

void test_alloc_zero(void) {
    void* ptr = ether_alloc(0);
    ASSERT(ptr == NULL);
}

void test_free_null(void) {
    // Should not crash
    ether_free(NULL);
}

void test_multiple_allocs(void) {
    void* ptrs[100];

    // Allocate many blocks
    for (int i = 0; i < 100; i++) {
        ptrs[i] = ether_alloc(64);
        ASSERT(ptrs[i] != NULL);
    }

    // Free all
    for (int i = 0; i < 100; i++) {
        ether_free(ptrs[i]);
    }
}

void test_write_read(void) {
    void* ptr = ether_alloc(256);
    ASSERT(ptr != NULL);

    const char* msg = "Hello, Ether!";
    size_t len = strlen(msg) + 1;

    // Write data
    int ret = ether_write(ptr, msg, len);
    ASSERT(ret == ETHER_OK);

    // Read back
    char buffer[256] = {0};
    ret = ether_read(ptr, buffer, len);
    ASSERT(ret == ETHER_OK);
    ASSERT(strcmp(buffer, msg) == 0);

    ether_free(ptr);
}

void test_write_overflow(void) {
    void* ptr = ether_alloc(10);
    ASSERT(ptr != NULL);

    char big_data[100];
    memset(big_data, 'X', 100);

    // Should fail - data larger than block
    int ret = ether_write(ptr, big_data, 100);
    ASSERT(ret == ETHER_ERR_OVERFLOW);

    ether_free(ptr);
}

void test_realloc_grow(void) {
    void* ptr = ether_alloc(50);
    ASSERT(ptr != NULL);

    const char* msg = "Test data";
    ether_write(ptr, msg, strlen(msg) + 1);

    // Grow the block
    ptr = ether_realloc(ptr, 200);
    ASSERT(ptr != NULL);
    ASSERT(ether_size(ptr) == 200);

    // Original data should be preserved
    char buffer[50];
    ether_read(ptr, buffer, strlen(msg) + 1);
    ASSERT(strcmp(buffer, msg) == 0);

    ether_free(ptr);
}

void test_realloc_shrink(void) {
    void* ptr = ether_alloc(200);
    ASSERT(ptr != NULL);

    // Shrink the block
    ptr = ether_realloc(ptr, 50);
    ASSERT(ptr != NULL);
    ASSERT(ether_size(ptr) == 50);

    ether_free(ptr);
}

void test_realloc_null(void) {
    // realloc(NULL, size) should behave like alloc(size)
    void* ptr = ether_realloc(NULL, 100);
    ASSERT(ptr != NULL);
    ASSERT(ether_size(ptr) == 100);
    ether_free(ptr);
}

void test_realloc_zero(void) {
    void* ptr = ether_alloc(100);
    ASSERT(ptr != NULL);

    // realloc(ptr, 0) should behave like free(ptr)
    ptr = ether_realloc(ptr, 0);
    ASSERT(ptr == NULL);
}

void test_large_alloc(void) {
    // 1 MB allocation
    size_t size = 1024 * 1024;
    void* ptr = ether_alloc(size);
    ASSERT(ptr != NULL);

    // Write pattern
    uint8_t* data = malloc(size);
    ASSERT(data != NULL);
    memset(data, 0xAB, size);

    int ret = ether_write(ptr, data, size);
    ASSERT(ret == ETHER_OK);

    // Read back and verify
    uint8_t* verify = malloc(size);
    ASSERT(verify != NULL);
    ret = ether_read(ptr, verify, size);
    ASSERT(ret == ETHER_OK);
    ASSERT(memcmp(data, verify, size) == 0);

    free(data);
    free(verify);
    ether_free(ptr);
}

void test_stats(void) {
    ether_reset_stats();

    void* ptr1 = ether_alloc(100);
    void* ptr2 = ether_alloc(200);

    ether_stats_t stats = ether_get_stats();
    ASSERT(stats.num_allocs == 2);
    ASSERT(stats.total_allocated == 300);
    ASSERT(stats.current_usage == 300);

    ether_free(ptr1);

    stats = ether_get_stats();
    ASSERT(stats.num_frees == 1);
    ASSERT(stats.current_usage == 200);

    ether_free(ptr2);

    stats = ether_get_stats();
    ASSERT(stats.current_usage == 0);
}

void test_memory_zero_init(void) {
    // Allocated memory should be zero-initialized
    size_t size = 1024;
    uint8_t* ptr = (uint8_t*)ether_alloc(size);
    ASSERT(ptr != NULL);

    for (size_t i = 0; i < size; i++) {
        ASSERT(ptr[i] == 0);
    }

    ether_free(ptr);
}

void test_error_strings(void) {
    // Test that error strings are not NULL
    ASSERT(ether_strerror(ETHER_OK) != NULL);
    ASSERT(ether_strerror(ETHER_ERR_NOMEM) != NULL);
    ASSERT(ether_strerror(ETHER_ERR_INVALID) != NULL);
    ASSERT(ether_strerror(ETHER_ERR_CORRUPT) != NULL);
    ASSERT(ether_strerror(ETHER_ERR_OVERFLOW) != NULL);
}

// =============================================================================
// MAIN
// =============================================================================

int main(void) {
    printf("\n");
    printf("===========================================\n");
    printf("  Ether Allocator Test Suite\n");
    printf("===========================================\n\n");

    TEST(test_basic_alloc_free);
    TEST(test_alloc_zero);
    TEST(test_free_null);
    TEST(test_multiple_allocs);
    TEST(test_write_read);
    TEST(test_write_overflow);
    TEST(test_realloc_grow);
    TEST(test_realloc_shrink);
    TEST(test_realloc_null);
    TEST(test_realloc_zero);
    TEST(test_large_alloc);
    TEST(test_stats);
    TEST(test_memory_zero_init);
    TEST(test_error_strings);

    printf("\n");
    printf("===========================================\n");
    printf("  Results: %d/%d tests passed\n", tests_passed, tests_run);
    printf("===========================================\n\n");

    ether_dump_state();

    return (tests_passed == tests_run) ? 0 : 1;
}