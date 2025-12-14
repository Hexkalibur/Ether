# Ether

**Distributed Resource Allocation System**

Ether is a system for allocating RAM, CPU, and GPU resources across networked machines using simple, familiar APIs. The goal is to make distributed computing feel as natural as local programming.

```c
// Remote memory - as simple as malloc()
void* ptr = ether_rmalloc(conn, 1024);
ether_rwrite(conn, ptr, data, len);
ether_rread(conn, ptr, buffer, len);
ether_rfree(conn, ptr);

// Future: Remote process spawning
uint64_t pid = ether_rspawn(conn, "./compute_task", args);
ether_rwait(conn, pid);
```

---

## Project Status

**Current Phase: Phase 1 Complete - Single Node Memory Allocation**

| Component | Status | Description |
|-----------|--------|-------------|
| Memory Allocator | Done | Local allocator with hidden headers, secure wipe, corruption detection |
| Wire Protocol | Done | Binary protocol with network byte order serialization |
| TCP Server | Done | Single-threaded daemon handling memory operations |
| Client Library | Done | Remote memory API (rmalloc, rfree, rwrite, rread) |
| Process Spawning | Planned | Fork/exec with I/O redirection and monitoring |
| Multi-Node | Planned | Node discovery, registration, and coordination |
| GPU Support | Planned | NVIDIA NVML integration for GPU resource tracking |

---

## Documentation

This documentation is split into multiple pages for clarity:

| Document | Description |
|----------|-------------|
| [Architecture Overview](docs/ARCHITECTURE.md) | System design, component relationships, data flow |
| [Memory Allocator](docs/ALLOCATOR.md) | How the allocator works, header structure, pointer arithmetic |
| [Wire Protocol](docs/PROTOCOL.md) | Message format, serialization, command reference |
| [Server Implementation](docs/SERVER.md) | etherd daemon, handle mapping, client handling |
| [Client Library](docs/CLIENT.md) | Connection management, handle cache, remote operations |
| [Roadmap](docs/ROADMAP.md) | Future features, challenges, implementation plan |

---

## Quick Start

### Building

```bash
mkdir build && cd build
cmake ..
make
```

### Running Tests

```bash
./test_allocator
./test_protocol
```

Expected output:
```
===========================================
  Ether Allocator Test Suite
===========================================

  Testing: test_basic_alloc_free          PASSED
  Testing: test_alloc_zero                PASSED
  ...
  Results: 14/14 tests passed
```

### Starting the Server

```bash
./etherd
# Or with custom port:
./etherd 8888
```

### Running the Client Example

```bash
# In another terminal:
./simple_client localhost 9999
```

---

## Directory Structure

```
ether/
├── include/ether/
│   ├── ether.h          # Core allocator API
│   ├── protocol.h       # Wire protocol definitions
│   └── client.h         # Client library API
├── src/
│   ├── allocator.c      # Memory allocator implementation
│   ├── protocol.c       # Message serialization
│   ├── server.c         # etherd daemon
│   └── client.c         # Client library
├── tests/
│   ├── test_allocator.c # Allocator test suite
│   └── test_protocol.c  # Protocol test suite
├── examples/
│   ├── echo_server.c    # Basic TCP server example
│   └── simple_client.c  # Client usage demonstration
├── docs/
│   ├── ARCHITECTURE.md  # System architecture
│   ├── ALLOCATOR.md     # Allocator internals
│   ├── PROTOCOL.md      # Protocol specification
│   ├── SERVER.md        # Server documentation
│   ├── CLIENT.md        # Client documentation
│   └── ROADMAP.md       # Future development
├── CMakeLists.txt       # Build configuration
├── LICENSE              # GPL-3.0
└── README.md            # This file
```

---

## Core Concepts

### Why Ether?

Existing solutions for distributed computing (Kubernetes, Ray, SLURM) are powerful but complex. Ether aims to provide a simpler abstraction:

| What You Want | Traditional | Ether |
|---------------|-------------|-------|
| Remote memory | Redis/Memcached + serialization | `ether_rmalloc()` |
| Distributed tasks | Container orchestration | `ether_rspawn()` |
| Resource scheduling | YAML configurations | Automatic based on requirements |

### Design Principles

1. **Familiar APIs** - If you know malloc/free, you know Ether
2. **Transparent Distribution** - Same code works locally and remotely
3. **Security by Default** - Memory is wiped before freeing
4. **Minimal Dependencies** - Pure C, POSIX sockets, no external libraries

---

## Building Blocks

### 1. Memory Allocator

The foundation. A custom allocator that tracks metadata in hidden headers:

```
Memory Layout:
┌─────────────────────┬──────────────────────────┐
│   Header (24B)      │   User Data              │
│ magic, flags, size  │   <-- pointer returned   │
└─────────────────────┴──────────────────────────┘
```

See [ALLOCATOR.md](docs/ALLOCATOR.md) for details.

### 2. Wire Protocol

Binary protocol for efficient network communication:

```
Message Header (24 bytes):
┌────────┬─────┬─────┬───────┬────────┬──────┬──────────┐
│ Magic  │ Ver │ Cmd │ Flags │ Handle │ Size │ Reserved │
│ 4B     │ 1B  │ 1B  │ 2B    │ 8B     │ 4B   │ 4B       │
└────────┴─────┴─────┴───────┴────────┴──────┴──────────┘
```

See [PROTOCOL.md](docs/PROTOCOL.md) for details.

### 3. Server (etherd)

TCP daemon that manages memory on behalf of clients:

```
Client Request --> Deserialize --> Execute --> Respond
     │                                │
     └── ALLOC 1024 ──────────────────┴── handle=0x1
```

See [SERVER.md](docs/SERVER.md) for details.

### 4. Client Library

Provides the user-facing API and maintains handle mappings:

```
User calls ether_rmalloc(conn, 100)
    │
    ├── Send ALLOC request to server
    ├── Receive handle from server
    ├── Allocate local buffer
    ├── Store mapping: local_ptr --> remote_handle
    └── Return local_ptr to user
```

See [CLIENT.md](docs/CLIENT.md) for details.

---

## What Works Today

1. **Local Memory Allocation**
   - Allocate, free, realloc with metadata tracking
   - Secure wipe on free
   - Corruption and double-free detection
   - Statistics tracking

2. **Remote Memory Operations**
   - Connect to server
   - Allocate memory on server
   - Write data to remote memory
   - Read data from remote memory
   - Free remote memory

3. **Protocol**
   - Message creation and validation
   - Network byte order serialization
   - All basic commands implemented

---

## What Comes Next

See [ROADMAP.md](docs/ROADMAP.md) for the complete development plan. Key milestones:

1. **Phase 2: Process Spawning** - Run commands on remote nodes
2. **Phase 3: Multi-Node** - Cluster coordination and discovery
3. **Phase 4: GPU Support** - NVIDIA GPU resource tracking
4. **Phase 5: Production** - Python bindings, benchmarks, hardening

---

## License

GPL-3.0 - See [LICENSE](LICENSE) for details.
