来自：https://github.com/jnk0le/Ring-Buffer
优化要点概述
减少原子操作次数：将多次加载的原子变量存储在局部变量中，避免重复加载。
优化内存屏障：使用 std::atomic_thread_fence 替代 std::atomic_signal_fence，确保正确的内存顺序。
合并重复代码：使用内部辅助模板函数来合并重复的 insert 和 remove 操作。
批量数据操作：在 writeBuff 和 readBuff 中使用 std::memcpy 进行批量数据传输，提升缓存利用率和数据传输效率。
避免伪共享：通过填充结构体，确保 head 和 tail 位于不同的缓存行中。
内存对齐：确保 data_buff 按缓存行对齐，提高缓存命中率。
内联小函数：将小型函数内联，减少函数调用开销。
使用更高效的数据类型：根据需求调整 index_t 的类型，确保其大小与性能需求匹配。
