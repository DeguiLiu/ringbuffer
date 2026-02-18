# SPSC Ring Buffer 设计文档

> 本文档对应博客: https://deguiliu.github.io/tech-notes/posts/performance/spsc_ringbuffer_design/

## 1. 为什么是 SPSC 而不是 MPMC

在嵌入式系统中，许多数据通道天然就是**单生产者单消费者**的：

| 场景 | 生产者 | 消费者 |
| --- | --- | --- |
| ADC 采样 | DMA 完成中断 | 处理线程 |
| 串口接收 | UART ISR | 协议解析线程 |
| 日志系统 | 应用线程 | 日志写盘线程 |
| 传感器数据 | 采集线程 | 融合线程 |

SPSC 可以做到 **wait-free**（最坏情况也是 O(1)），而 MPMC 只能做到 **lock-free**（全局保证进展，但单个线程可能饿死）。

## 2. 为什么不用对象池

对象池引入了三个性能代价：

1. **mutex 在每次存取上**：即使是无竞争的 futex 快速路径，在 ARM Cortex-A72 上也需要 ~20-40ns；而 SPSC 环形缓冲的 `Push`/`Pop` 是 wait-free 的 ~5-8ns。

2. **shared_ptr 原子引用计数**：SPSC 场景中数据是单向传递，所有权始终明确，引用计数完全多余。

3. **queue 动态增长**：内存预算无法在编译期确定。

| 维度 | 对象池 | SPSC 环形缓冲 |
| --- | --- | --- |
| 同步机制 | mutex (futex) | 无锁 wait-free |
| 引用管理 | atomic refcount | 值语义 memcpy |
| 内存增长 | queue 动态扩展 | 编译期固定数组 |
| 最坏延迟 | futex slow path ~us | O(1) ~ns |

## 3. 整体架构

```
Producer Thread                    Consumer Thread
    |                                  |
    | Push(data)                       | Pop(data)
    |   load head_ (relaxed)           |   load tail_ (relaxed)
    |   load tail_ (acquire)           |   load head_ (acquire)
    |   if full -> return false        |   if empty -> return false
    |   write data_buff_[head & mask]  |   read data_buff_[tail & mask]
    |   store head_+1 (release)        |   store tail_+1 (release)
    |                                  |
    v                                  v

+----+----+----+----+----+----+----+----+
| D0 | D1 | D2 | D3 | D4 | D5 | D6 | D7 |  data_buff_[8]
+----+----+----+----+----+----+----+----+
           ^                   ^
           tail_               head_
           (consumer writes)   (producer writes)
```

核心数据结构：

```cpp
PaddedIndex head_;                       // 生产者写，消费者读
PaddedIndex tail_;                       // 消费者写，生产者读
alignas(64) T data_buff_[BufferSize]{};  // 环形存储
```

## 4. 缓存行对齐与 false sharing 消除

`head_` 由生产者频繁写入，`tail_` 由消费者频繁写入。如果这两个变量位于同一条缓存行（通常 64 字节），会发生 **false sharing**。

解决方案：使用 `PaddedIndex` 将 `head_` 和 `tail_` 分别填充到独立的缓存行：

```cpp
template<typename IndexT>
struct PaddedIndex {
    alignas(64) std::atomic<IndexT> value;
};
```

这样 `head_` 和 `tail_` 位于不同的缓存行，避免了 false sharing 导致的性能下降。

## 5. 内存序与硬件屏障

### 5.1 Producer Push 流程

```cpp
bool Push(const T& data) {
    IndexT head = head_.value.load(std::memory_order_relaxed);
    IndexT tail = tail_.value.load(std::memory_order_acquire);

    if ((head - tail) >= Capacity()) return false;  // 满

    data_buff_[head & mask_] = data;  // 写数据
    head_.value.store(head + 1, std::memory_order_release);  // 发布
    return true;
}
```

**关键点**：
- `load(relaxed)` 自己的 `head_`：无需屏障，单线程访问
- `load(acquire)` 对方的 `tail_`：获取消费者的进度，需要 acquire 屏障
- `store(release)` 更新 `head_`：发布新数据，需要 release 屏障

### 5.2 Consumer Pop 流程

```cpp
bool Pop(T& data) {
    IndexT tail = tail_.value.load(std::memory_order_relaxed);
    IndexT head = head_.value.load(std::memory_order_acquire);

    if (tail == head) return false;  // 空

    data = data_buff_[tail & mask_];  // 读数据
    tail_.value.store(tail + 1, std::memory_order_release);  // 发布
    return true;
}
```

### 5.3 FakeTSO 模式（单核 MCU）

对于单核 MCU（无 D-Cache 或 I-Cache），硬件内存屏障是多余的。`FakeTSO` 模式使用 `memory_order_relaxed` 替代 acquire/release：

```cpp
if constexpr (FakeTSO) {
    tail_.value.load(std::memory_order_relaxed);
    head_.value.store(head + 1, std::memory_order_relaxed);
    std::atomic_signal_fence(std::memory_order_release);  // 编译器屏障
} else {
    // 多核模式：acquire/release
}
```

## 6. 幂次方大小与位掩码

缓冲区大小必须是 2 的幂次方，使用位掩码替代模运算：

```cpp
static_assert((BufferSize & (BufferSize - 1)) == 0, "BufferSize must be power of 2");
IndexT mask_ = BufferSize - 1;

// 快速索引计算
data_buff_[head & mask_] = data;  // 替代 data_buff_[head % BufferSize]
```

位掩码运算比模运算快 3-5 倍。

## 7. 批量操作优化

### 7.1 PushBatch

```cpp
size_t PushBatch(const T* src, size_t count) {
    IndexT head = head_.value.load(std::memory_order_relaxed);
    IndexT tail = tail_.value.load(std::memory_order_acquire);

    size_t available = Capacity() - (head - tail);
    size_t to_push = std::min(count, available);

    if (to_push == 0) return 0;

    // 分两段 memcpy（环形缓冲的两端）
    size_t first_part = std::min(to_push, Capacity() - (head & mask_));
    std::memcpy(&data_buff_[head & mask_], src, first_part * sizeof(T));

    if (to_push > first_part) {
        std::memcpy(&data_buff_[0], src + first_part,
                    (to_push - first_part) * sizeof(T));
    }

    head_.value.store(head + to_push, std::memory_order_release);
    return to_push;
}
```

批量操作通过 `memcpy` 减少循环开销，吞吐量提升 2-3 倍。

## 8. 与原始实现的改进

| 问题 | 原始实现 | 本项目修正 |
| --- | --- | --- |
| ProducerClear 所有权 | 修改 `tail_`（消费者所有） | 修改 `head_`（生产者所有） |
| 冗余屏障 | `atomic_thread_fence` + `store(release)` | 仅 `store(release)` |
| Remove() 歧义 | 3 个重载，返回类型不同 | Push/Pop/Discard 清晰命名 |
| 回调类型 | 仅函数指针 | 模板化（lambda、std::function） |
| 移动语义 | 未暴露 | `Push(T&&)` 支持 |
| 命名空间 | `utility` | `spsc` |

## 9. 性能基准

在 ARM Cortex-A72 (2.0 GHz) 上的典型性能：

| 操作 | 延迟 | 吞吐量 |
| --- | --- | --- |
| Push (单个) | ~5-8ns | ~200M ops/s |
| Pop (单个) | ~5-8ns | ~200M ops/s |
| PushBatch (1024 元素) | ~0.5ns/elem | ~2B elems/s |
| PopBatch (1024 元素) | ~0.5ns/elem | ~2B elems/s |

## 10. 使用场景

- **实时日志系统**：应用线程 → 日志写盘线程
- **传感器数据采集**：DMA/ISR → 处理线程
- **音视频处理**：编码线程 → 输出线程
- **网络数据转发**：接收线程 → 处理线程
- **嵌入式消息队列**：任务间通信

## 参考资源

- 原始实现: [jnk0le/Ring-Buffer](https://github.com/jnk0le/Ring-Buffer)
- 内存屏障硬件原理: [Store Buffer 到 ARM DMB/DSB/ISB](https://deguiliu.github.io/tech-notes/posts/performance/memory_barrier_hardware/)
- 无锁编程基础: [从 CAS 到三种队列模式](https://deguiliu.github.io/tech-notes/posts/performance/lockfree_programming_fundamentals/)
- 无锁异步日志: [Per-Thread SPSC 应用](https://deguiliu.github.io/tech-notes/posts/performance/lockfree_async_log/)
