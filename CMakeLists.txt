cmake_minimum_required(VERSION 3.15)
project(ether C)

# C11
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Position Independent Code (required for shared libraries)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

#==========================================================================================================================================================================================
# Compiler flags
#---GLOBAL
set(CMAKE_C_FLAGS "-Wall -Wextra -Wpedantic -fstack-protector-strong")
#---RELEASE
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG -flto")
#---DEBUG
set(CMAKE_C_FLAGS_DEBUG "-O0 -g -fsanitize=address,undefined -fno-omit-frame-pointer")
#==========================================================================================================================================================================================

# Include directories
include_directories(${PROJECT_SOURCE_DIR}/include)

# CORE LIBRARY: libether
add_library(ether SHARED
        src/allocator.c
        src/protocol.c
)

# SERVER DAEMON: etherd
add_executable(etherd src/server.c)
target_link_libraries(etherd ether)

# CLIENT LIBRARY: libether_client
add_library(ether_client SHARED src/client.c)
target_link_libraries(ether_client ether)

# EXAMPLES
add_executable(echo_server examples/echo_server.c)

add_executable(simple_client examples/simple_client.c)
target_link_libraries(simple_client ether_client)

# TESTS
enable_testing()

add_executable(test_allocator tests/test_allocator.c)
target_link_libraries(test_allocator ether)
add_test(NAME AllocatorTests COMMAND test_allocator)

add_executable(test_protocol tests/test_protocol.c)
target_link_libraries(test_protocol ether)
add_test(NAME ProtocolTests COMMAND test_protocol)

#add_executable(test_pointer tests/test_pointer.c)
#target_link_libraries(test_pointer ether)
#add_test(NAME PointerTests COMMAND test_pointer)

#MAIN
add_executable(main main.c)
target_link_libraries(main ether)
add_test(NAME Main COMMAND main)

# INSTALL
install(TARGETS ether ether_client etherd
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin
)
install(DIRECTORY include/ether DESTINATION include)

# Print config
message(STATUS "===========================================")
message(STATUS "Ether - Memory through the Ether")
message(STATUS "===========================================")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C Compiler: ${CMAKE_C_COMPILER}")