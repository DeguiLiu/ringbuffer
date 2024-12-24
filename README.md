# spsc::Ringbuffer

Lock-free, wait-free SPSC (Single-Producer Single-Consumer) ring buffer for C++14.

Header-only. Zero dependencies. Cache-line aligned to avoid false sharing.

Based on [jnk0le/Ring-Buffer](https://github.com/jnk0le/Ring-Buffer), with correctness fixes and API redesign.

## Features

- Lock-free, wait-free for exactly one producer and one consumer thread
- Power-of-2 buffer size with bitmask (no modulo)
- Cache-line padded head/tail indices (no false sharing)
- Batch push/pop with `memcpy` for throughput
- Templatized callbacks (lambda, `std::function`, function pointer)
- `FakeTSO` mode for single-core MCU (skips hardware memory barriers)
- Configurable index type (`uint8_t` to `size_t`)

## Quick Start

```cpp
#include <spsc/ringbuffer.hpp>

spsc::Ringbuffer<int, 1024> rb;

// Producer
rb.Push(42);

// Consumer
int val;
if (rb.Pop(val)) {
    // val == 42
}
```

## API Reference

### Template Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `T` | - | Element type. Must be trivially copyable. |
| `BufferSize` | 16 | Capacity. Must be a power of 2. |
| `FakeTSO` | false | If true, use relaxed ordering (single-core MCU). |
| `IndexT` | `std::size_t` | Unsigned index type. |

### Producer API (call from producer thread only)

| Method | Description |
|--------|-------------|
| `bool Push(const T&)` | Push by copy. Returns false if full. |
| `bool Push(T&&)` | Push by move. Returns false if full. |
| `bool PushFromCallback(Callable&&)` | Push element produced by callback. Callback not called if full. |
| `size_t PushBatch(const T*, size_t)` | Batch push. Returns count actually pushed. |
| `size_t PushBatch(const T*, size_t, Callable&&)` | Batch push with notification callback. |
| `void ProducerClear()` | Clear buffer (sets head = tail). |

### Consumer API (call from consumer thread only)

| Method | Description |
|--------|-------------|
| `bool Pop(T&)` | Pop front element. Returns false if empty. |
| `size_t PopBatch(T*, size_t)` | Batch pop. Returns count actually popped. |
| `size_t PopBatch(T*, size_t, Callable&&)` | Batch pop with notification callback. |
| `size_t Discard(size_t count = 1)` | Discard elements without reading. Returns count discarded. |
| `T* Peek()` | Peek at front element. Returns nullptr if empty. |
| `T* At(size_t index)` | Access n-th element with bounds check. |
| `const T& operator[](size_t)` | Unchecked access. UB if index >= Size(). |
| `void ConsumerClear()` | Clear buffer (sets tail = head). |

### Query API (either thread)

| Method | Description |
|--------|-------------|
| `IndexT Size()` | Number of readable elements. |
| `IndexT Available()` | Number of writable slots. |
| `bool IsEmpty()` | True if no readable elements. |
| `bool IsFull()` | True if no writable slots. |
| `static constexpr size_t Capacity()` | Buffer capacity (compile-time). |

## Thread Safety

- Exactly ONE producer thread may call Push/PushBatch/PushFromCallback/ProducerClear.
- Exactly ONE consumer thread may call Pop/PopBatch/Discard/Peek/At/ConsumerClear.
- Size/Available/IsEmpty/IsFull/Capacity may be called from either side.
- Multiple producers or multiple consumers is UNDEFINED BEHAVIOR.

## Build

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
ctest --output-on-failure
```

Options:

| CMake Option | Default | Description |
|-------------|---------|-------------|
| `RINGBUFFER_BUILD_TESTS` | ON | Build Catch2 tests |
| `RINGBUFFER_BUILD_EXAMPLES` | ON | Build example programs |

## Integration

Header-only. Copy `include/spsc/ringbuffer.hpp` into your project, or use CMake FetchContent:

```cmake
FetchContent_Declare(ringbuffer
    GIT_REPOSITORY https://gitee.com/liudegui/ringbuffer2.git
    GIT_TAG master)
FetchContent_MakeAvailable(ringbuffer)
target_link_libraries(your_target PRIVATE ringbuffer)
```

## Changes from Original

| Issue | Original | Fixed |
|-------|----------|-------|
| ProducerClear ownership | Modified `tail_` (consumer-owned) | Modifies `head_` (producer-owned) |
| Redundant fences | `atomic_thread_fence` + `store(release)` | Store with release only |
| Remove() ambiguity | 3 overloads, different return types | Push/Pop/Discard naming |
| Callback type | Function pointer only | Templatized (lambda, std::function) |
| Move semantics | Not exposed | `Push(T&&)` |
| Namespace | `utility` | `spsc` |

## License

MIT
