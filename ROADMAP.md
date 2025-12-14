# Roadmap

[Back to README](../README.md)

This document outlines the planned development phases for Ether, the challenges to overcome, and implementation strategies.

---

## Current State: Phase 1 Complete

What we have built:

| Component | Status | Lines of Code |
|-----------|--------|---------------|
| Memory Allocator | Complete | ~340 |
| Wire Protocol | Complete | ~180 |
| TCP Server | Complete | ~280 |
| Client Library | Complete | ~320 |
| Test Suites | Complete | ~400 |
| Total | | ~1,520 |

Working features:
- Local memory allocation with metadata tracking
- Secure wipe on free
- Corruption and double-free detection
- Binary network protocol
- Remote memory operations (rmalloc, rfree, rwrite, rread)
- Client-server communication

---

## Phase 2: Process Spawning

**Goal:** Run commands on remote nodes with I/O capture and resource limits.

### New API

```c
// Resource requirements
typedef struct {
    size_t memory_mb;     // RAM requirement
    int    cpu_cores;     // CPU cores needed
    int    gpu_count;     // GPUs needed
    int    timeout_sec;   // Max execution time
} ether_resources_t;

// Process status
typedef enum {
    ETHER_PROC_PENDING,
    ETHER_PROC_RUNNING,
    ETHER_PROC_COMPLETED,
    ETHER_PROC_FAILED,
    ETHER_PROC_KILLED,
    ETHER_PROC_TIMEOUT
} ether_proc_status_t;

// Spawn a remote process
uint64_t ether_rspawn(ether_conn_t* conn, 
                      const char* command,
                      char* const argv[],
                      ether_resources_t* resources);

// Check process status
ether_proc_status_t ether_rstatus(ether_conn_t* conn, uint64_t pid);

// Wait for process completion
int ether_rwait(ether_conn_t* conn, uint64_t pid);

// Kill a process
int ether_rkill(ether_conn_t* conn, uint64_t pid);

// Get process output
ssize_t ether_rget_stdout(ether_conn_t* conn, uint64_t pid, 
                          char* buffer, size_t len);
ssize_t ether_rget_stderr(ether_conn_t* conn, uint64_t pid,
                          char* buffer, size_t len);
```

### Implementation Challenges

**Challenge 1: Process Isolation**

Problem: Spawned processes should be isolated from the server.

Solutions:
- Use `fork()` + `exec()` with restricted environment
- Set resource limits with `setrlimit()`
- Change to sandboxed directory
- Consider `seccomp` for syscall filtering

```c
void spawn_process(const char* cmd, char* argv[]) {
    pid_t pid = fork();
    if (pid == 0) {
        // Child process
        
        // Restrict resources
        struct rlimit rl = {.rlim_cur = 1024*1024*1024, .rlim_max = 1024*1024*1024};
        setrlimit(RLIMIT_AS, &rl);  // 1GB memory limit
        
        // Change directory
        chdir("/tmp/ether_sandbox");
        
        // Redirect I/O
        int stdout_fd = open("stdout.log", O_WRONLY|O_CREAT, 0644);
        int stderr_fd = open("stderr.log", O_WRONLY|O_CREAT, 0644);
        dup2(stdout_fd, STDOUT_FILENO);
        dup2(stderr_fd, STDERR_FILENO);
        
        // Execute
        execvp(cmd, argv);
        exit(127);  // exec failed
    }
    // Parent tracks pid
}
```

**Challenge 2: I/O Capture**

Problem: Need to capture stdout/stderr without blocking.

Solutions:
- Redirect to files, read on demand
- Use pipes with non-blocking I/O
- Poll for output periodically

```c
typedef struct {
    uint64_t handle;
    pid_t    pid;
    int      stdout_fd;
    int      stderr_fd;
    char     stdout_path[256];
    char     stderr_path[256];
    ether_proc_status_t status;
    int      exit_code;
} process_entry_t;
```

**Challenge 3: Timeout Handling**

Problem: Processes might run forever.

Solution: Use `alarm()` or timer thread to enforce timeouts.

```c
// In parent process
void check_process_timeout(process_entry_t* proc) {
    time_t now = time(NULL);
    if (now - proc->start_time > proc->timeout) {
        kill(proc->pid, SIGKILL);
        proc->status = ETHER_PROC_TIMEOUT;
    }
}
```

### New Protocol Commands

```c
ETHER_CMD_SPAWN       = 0x30  // Start process
ETHER_CMD_PROC_STATUS = 0x31  // Get status
ETHER_CMD_PROC_KILL   = 0x32  // Kill process
ETHER_CMD_PROC_STDOUT = 0x33  // Get stdout
ETHER_CMD_PROC_STDERR = 0x34  // Get stderr
ETHER_CMD_PROC_WAIT   = 0x35  // Wait for completion
```

### Estimated Effort

- Process spawning: 2-3 days
- I/O capture: 1-2 days
- Protocol extensions: 1 day
- Testing: 2 days
- **Total: ~1 week**

---

## Phase 3: Multi-Node Cluster

**Goal:** Multiple servers coordinating as a cluster with automatic resource discovery.

### Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                      COORDINATOR NODE                            │
│                                                                  │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐ │
│  │   Node      │  │  Resource   │  │   Request               │ │
│  │   Registry  │  │  Scheduler  │  │   Router                │ │
│  └─────────────┘  └─────────────┘  └─────────────────────────┘ │
│                                                                  │
└──────────┬─────────────────┬─────────────────┬──────────────────┘
           │                 │                 │
           ▼                 ▼                 ▼
    ┌──────────┐      ┌──────────┐      ┌──────────┐
    │  Node 1  │      │  Node 2  │      │  Node 3  │
    │ 64GB RAM │      │ 32GB RAM │      │ 128GB    │
    │ 8 cores  │      │ 4 cores  │      │ 16 cores │
    │ 0 GPUs   │      │ 2 GPUs   │      │ 4 GPUs   │
    └──────────┘      └──────────┘      └──────────┘
```

### New Concepts

**Node Registration:**
```c
typedef struct {
    char     node_id[64];     // Unique identifier
    char     hostname[256];   // Network hostname
    int      port;            // etherd port
    size_t   total_memory;    // Total RAM
    size_t   free_memory;     // Available RAM
    int      cpu_cores;       // Total cores
    int      gpu_count;       // Total GPUs
    time_t   last_heartbeat;  // Last seen
} ether_node_info_t;
```

**Resource Request:**
```c
// Instead of connecting to specific node:
ether_conn_t* conn = ether_connect("node1.local", 9999);

// Connect to cluster, let it choose node:
ether_conn_t* conn = ether_cluster_connect("coordinator.local", 9999);

// Request with requirements:
ether_resources_t req = {.memory_mb = 1024, .cpu_cores = 4};
void* ptr = ether_rmalloc_with_resources(conn, size, &req);
```

### Implementation Challenges

**Challenge 1: Node Discovery**

Options:
- Manual configuration file
- Multicast/broadcast discovery
- Consul/etcd integration
- DNS-based discovery

Simplest for MVP: Configuration file

```ini
# /etc/ether/cluster.conf
[coordinator]
host = coord.local
port = 9999

[nodes]
node1 = node1.local:9999
node2 = node2.local:9999
node3 = node3.local:9999
```

**Challenge 2: Health Monitoring**

Need heartbeats to detect dead nodes:

```c
// Node sends heartbeat every 5 seconds
void send_heartbeat(ether_conn_t* coordinator) {
    ether_node_info_t info = get_local_info();
    ether_msg_t* msg = ether_msg_create(ETHER_CMD_HEARTBEAT, sizeof(info));
    memcpy(msg->payload, &info, sizeof(info));
    send_message(coordinator, msg);
}

// Coordinator marks nodes dead after 15 seconds
void check_node_health() {
    time_t now = time(NULL);
    for (int i = 0; i < num_nodes; i++) {
        if (now - nodes[i].last_heartbeat > 15) {
            nodes[i].status = NODE_DEAD;
        }
    }
}
```

**Challenge 3: Resource Scheduling**

When client requests resources, coordinator must choose node:

```c
ether_node_t* select_node(ether_resources_t* req) {
    ether_node_t* best = NULL;
    
    for (int i = 0; i < num_nodes; i++) {
        if (nodes[i].status != NODE_ALIVE) continue;
        if (nodes[i].free_memory < req->memory_mb * 1024 * 1024) continue;
        if (nodes[i].cpu_cores < req->cpu_cores) continue;
        if (nodes[i].gpu_count < req->gpu_count) continue;
        
        // Simple strategy: most free memory
        if (!best || nodes[i].free_memory > best->free_memory) {
            best = &nodes[i];
        }
    }
    
    return best;
}
```

**Challenge 4: Handle Migration**

What happens when a node dies?

Options:
- Data is lost (simple, current approach)
- Replicate to multiple nodes (complex, future)
- Checkpoint to disk (medium complexity)

For MVP: Accept data loss, notify client.

### New Protocol Commands

```c
ETHER_CMD_NODE_JOIN     = 0x40  // Node joins cluster
ETHER_CMD_NODE_LEAVE    = 0x41  // Node leaves cluster
ETHER_CMD_HEARTBEAT     = 0x42  // Node heartbeat
ETHER_CMD_NODE_LIST     = 0x43  // List all nodes
ETHER_CMD_ROUTE_REQUEST = 0x44  // Route request to best node
```

### Estimated Effort

- Node registry: 2 days
- Heartbeat system: 2 days
- Resource scheduler: 3 days
- Client routing: 2 days
- Testing: 3 days
- **Total: ~2 weeks**

---

## Phase 4: GPU Support

**Goal:** Track and allocate GPU resources.

### API Addition

```c
typedef struct {
    int    index;           // GPU index (0, 1, 2...)
    char   name[256];       // "NVIDIA GeForce RTX 3080"
    size_t memory_total;    // Total VRAM
    size_t memory_free;     // Available VRAM
    int    compute_cap;     // Compute capability (e.g., 86)
    float  utilization;     // Current usage %
    float  temperature;     // Current temp C
} ether_gpu_info_t;

// Get GPU information
int ether_get_gpu_count(ether_conn_t* conn);
int ether_get_gpu_info(ether_conn_t* conn, int index, ether_gpu_info_t* info);

// GPU memory allocation (future)
void* ether_gpu_malloc(ether_conn_t* conn, int gpu_index, size_t size);
void  ether_gpu_free(ether_conn_t* conn, void* ptr);
```

### Implementation

Use NVIDIA Management Library (NVML):

```c
#include <nvml.h>

int get_gpu_info(int index, ether_gpu_info_t* info) {
    nvmlDevice_t device;
    nvmlDeviceGetHandleByIndex(index, &device);
    
    nvmlDeviceGetName(device, info->name, sizeof(info->name));
    
    nvmlMemory_t memory;
    nvmlDeviceGetMemoryInfo(device, &memory);
    info->memory_total = memory.total;
    info->memory_free = memory.free;
    
    nvmlUtilization_t util;
    nvmlDeviceGetUtilizationRates(device, &util);
    info->utilization = util.gpu;
    
    unsigned int temp;
    nvmlDeviceGetTemperature(device, NVML_TEMPERATURE_GPU, &temp);
    info->temperature = temp;
    
    return 0;
}
```

### Challenges

**Challenge 1: CUDA Integration**

For actual GPU memory allocation, need CUDA:

```c
#include <cuda_runtime.h>

void* gpu_malloc(int gpu_index, size_t size) {
    cudaSetDevice(gpu_index);
    void* ptr;
    cudaError_t err = cudaMalloc(&ptr, size);
    if (err != cudaSuccess) return NULL;
    return ptr;
}
```

This requires:
- CUDA toolkit installed
- GPU drivers
- Linking with CUDA libraries

**Challenge 2: Cross-Network GPU Access**

GPU memory cannot be directly accessed over network. Options:
- Copy to host memory, transfer, copy back (slow)
- GPUDirect RDMA (requires special hardware)
- Focus on process spawning with GPU access (simpler)

For MVP: Just report GPU info, let spawned processes use GPUs.

### Estimated Effort

- NVML integration: 1-2 days
- GPU info API: 1 day
- Testing: 1 day
- **Total: ~3-4 days**

---

## Phase 5: Production Hardening

**Goal:** Make Ether production-ready.

### Security

1. **Authentication**
   - API keys
   - mTLS (mutual TLS)
   
2. **Encryption**
   - TLS for all connections
   - At-rest encryption for persistent data

3. **Authorization**
   - Per-user quotas
   - ACLs for resources

### Reliability

1. **Connection Handling**
   - Automatic reconnection
   - Connection pooling
   - Timeout configuration

2. **Error Recovery**
   - Graceful degradation
   - Retry logic
   - Circuit breakers

3. **Persistence**
   - Handle survival across restarts
   - State checkpointing

### Observability

1. **Metrics**
   - Prometheus integration
   - Memory usage
   - Request latency
   - Error rates

2. **Logging**
   - Structured logging (JSON)
   - Log levels
   - Log rotation

3. **Tracing**
   - Request tracing across nodes
   - Correlation IDs

### Performance

1. **Benchmarks**
   - Allocation throughput
   - Network latency
   - Comparison with alternatives

2. **Optimizations**
   - Memory pooling
   - Zero-copy where possible
   - Batch operations

---

## Phase 6: Language Bindings

**Goal:** Make Ether accessible from other languages.

### Python Bindings

```python
import ether

# Connect
conn = ether.connect("localhost", 9999)

# Allocate
ptr = ether.rmalloc(conn, 1024)

# Write/read
ether.rwrite(conn, ptr, b"Hello, Ether!")
data = ether.rread(conn, ptr, 1024)

# Free
ether.rfree(conn, ptr)

# Disconnect
ether.disconnect(conn)
```

Implementation options:
- ctypes (simplest)
- Cython (faster)
- pybind11 (C++)
- CFFI

### Other Languages

- **Rust** - bindgen + safe wrapper
- **Go** - cgo bindings
- **JavaScript/Node** - N-API bindings

---

## Timeline Summary

| Phase | Description | Estimated Time |
|-------|-------------|----------------|
| 1 | Memory Allocation (DONE) | - |
| 2 | Process Spawning | 1 week |
| 3 | Multi-Node Cluster | 2 weeks |
| 4 | GPU Support | 3-4 days |
| 5 | Production Hardening | 2-3 weeks |
| 6 | Language Bindings | 1 week per language |

---

## Contributing

This roadmap is a living document. Priorities may shift based on:
- User feedback
- Technical discoveries
- Resource availability

The codebase is designed for learning and experimentation. Each phase builds on the previous, and the code prioritizes clarity over premature optimization.
