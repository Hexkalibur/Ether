/**
 * Ether Protocol Test Suite
 *
 * Tests for message creation, serialization, and validation.
 *
 * Copyright (C) 2024 - Licensed under GPL-3.0
 */

#include "ether/protocol.h"
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

void test_msg_create(void) {
    ether_msg_t* msg = ether_msg_create(ETHER_CMD_PING, 0);
    ASSERT(msg != NULL);
    ASSERT(msg->header.magic == ETHER_MAGIC);
    ASSERT(msg->header.version == ETHER_PROTOCOL_VER);
    ASSERT(msg->header.command == ETHER_CMD_PING);
    ASSERT(msg->header.size == 0);
    ether_msg_free(msg);
}

void test_msg_with_payload(void) {
    ether_msg_t* msg = ether_msg_create(ETHER_CMD_WRITE, 100);
    ASSERT(msg != NULL);
    ASSERT(msg->header.size == 100);

    // Write to payload
    memset(msg->payload, 0xAB, 100);
    ASSERT(msg->payload[0] == 0xAB);
    ASSERT(msg->payload[99] == 0xAB);

    ether_msg_free(msg);
}

void test_msg_validate(void) {
    ether_msg_header_t header;

    // Valid header
    header.magic = ETHER_MAGIC;
    header.version = ETHER_PROTOCOL_VER;
    header.size = 0;
    ASSERT(ether_msg_validate(&header) == 1);

    // Invalid magic
    header.magic = 0xDEADBEEF;
    ASSERT(ether_msg_validate(&header) == 0);

    // Invalid version
    header.magic = ETHER_MAGIC;
    header.version = 99;
    ASSERT(ether_msg_validate(&header) == 0);

    // Invalid size (too large)
    header.version = ETHER_PROTOCOL_VER;
    header.size = ETHER_MAX_PAYLOAD + 1;
    ASSERT(ether_msg_validate(&header) == 0);
}

void test_serialization_roundtrip(void) {
    ether_msg_header_t original;
    original.magic = ETHER_MAGIC;
    original.version = ETHER_PROTOCOL_VER;
    original.command = ETHER_CMD_ALLOC;
    original.flags = 0x1234;
    original.handle = 0xDEADBEEFCAFEBABE;
    original.size = 12345;
    original.reserved = 0;

    // Serialize
    uint8_t buffer[ETHER_HEADER_SIZE];
    ether_msg_serialize_header(&original, buffer);

    // Deserialize
    ether_msg_header_t restored;
    ether_msg_deserialize_header(buffer, &restored);

    // Compare all fields
    ASSERT(restored.magic == original.magic);
    ASSERT(restored.version == original.version);
    ASSERT(restored.command == original.command);
    ASSERT(restored.flags == original.flags);
    ASSERT(restored.handle == original.handle);
    ASSERT(restored.size == original.size);
}

void test_all_commands(void) {
    ether_cmd_t cmds[] = {
        ETHER_CMD_PING, ETHER_CMD_PONG,
        ETHER_CMD_ALLOC, ETHER_CMD_FREE, ETHER_CMD_REALLOC,
        ETHER_CMD_WRITE, ETHER_CMD_READ,
        ETHER_CMD_OK, ETHER_CMD_ERROR
    };

    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        ether_msg_t* msg = ether_msg_create(cmds[i], 0);
        ASSERT(msg != NULL);
        ASSERT(msg->header.command == cmds[i]);
        ether_msg_free(msg);
    }
}

void test_total_size(void) {
    ether_msg_t* msg = ether_msg_create(ETHER_CMD_WRITE, 100);
    ASSERT(msg != NULL);

    size_t total = ether_msg_total_size(msg);
    ASSERT(total == ETHER_HEADER_SIZE + 100);

    ether_msg_free(msg);
}

void test_cmd_to_string(void) {
    // Test that all commands have non-NULL string representations
    ASSERT(ether_cmd_to_string(ETHER_CMD_PING) != NULL);
    ASSERT(ether_cmd_to_string(ETHER_CMD_PONG) != NULL);
    ASSERT(ether_cmd_to_string(ETHER_CMD_ALLOC) != NULL);
    ASSERT(ether_cmd_to_string(ETHER_CMD_FREE) != NULL);
    ASSERT(ether_cmd_to_string(ETHER_CMD_WRITE) != NULL);
    ASSERT(ether_cmd_to_string(ETHER_CMD_READ) != NULL);
    ASSERT(ether_cmd_to_string(ETHER_CMD_OK) != NULL);
    ASSERT(ether_cmd_to_string(ETHER_CMD_ERROR) != NULL);

    // Unknown command should also have a string
    ASSERT(ether_cmd_to_string(0x99) != NULL);
}

void test_header_size(void) {
    // Header should be exactly 24 bytes
    ASSERT(ETHER_HEADER_SIZE == 24);
    ASSERT(sizeof(ether_msg_header_t) == 24);
}

void test_null_handling(void) {
    // Functions should handle NULL gracefully
    ASSERT(ether_msg_validate(NULL) == 0);
    ASSERT(ether_msg_total_size(NULL) == 0);

    // These should not crash
    ether_msg_free(NULL);
    ether_msg_serialize_header(NULL, NULL);
    ether_msg_deserialize_header(NULL, NULL);
}

// =============================================================================
// MAIN
// =============================================================================

int main(void) {
    printf("\n");
    printf("===========================================\n");
    printf("  Ether Protocol Test Suite\n");
    printf("===========================================\n\n");

    TEST(test_msg_create);
    TEST(test_msg_with_payload);
    TEST(test_msg_validate);
    TEST(test_serialization_roundtrip);
    TEST(test_all_commands);
    TEST(test_total_size);
    TEST(test_cmd_to_string);
    TEST(test_header_size);
    TEST(test_null_handling);

    printf("\n");
    printf("===========================================\n");
    printf("  Results: %d/%d tests passed\n", tests_passed, tests_run);
    printf("===========================================\n\n");

    return (tests_passed == tests_run) ? 0 : 1;
}