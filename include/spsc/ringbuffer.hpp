#ifndef SPSC_RINGBUFFER_HPP
#define SPSC_RINGBUFFER_HPP

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>
#include <utility>

namespace spsc {

/// @brief Lock-free, wait-free SPSC (Single-Producer Single-Consumer) ring buffer.
///
/// @tparam T            Element type. Must be trivially copyable.
/// @tparam BufferSize   Capacity. Must be a power of 2.
/// @tparam FakeTSO      If true, omit hardware memory barriers (for single-core MCU / TSO arch).
/// @tparam IndexT       Index type. Must be unsigned. Defaults to std::size_t.
///
/// Thread safety:
///   - Exactly ONE producer thread may call Push/PushBatch/PushFromCallback/ProducerClear.
///   - Exactly ONE consumer thread may call Pop/PopBatch/Discard/Peek/At/ConsumerClear.
///   - Size/Available/IsEmpty/IsFull/Capacity may be called from either side.
///   - Using multiple producers or multiple consumers is UNDEFINED BEHAVIOR.
template <typename T, std::size_t BufferSize = 16, bool FakeTSO = false, typename IndexT = std::size_t>
class Ringbuffer {
 public:
  static_assert(BufferSize != 0, "Buffer size cannot be zero.");
  static_assert((BufferSize & (BufferSize - 1)) == 0, "Buffer size must be a power of 2.");
  static_assert(sizeof(IndexT) <= sizeof(std::size_t), "Index type size must not exceed size_t.");
  static_assert(std::is_unsigned<IndexT>::value, "Index type must be unsigned.");
  static_assert(BufferSize <= ((std::numeric_limits<IndexT>::max)() >> 1),
                "Buffer size is too large for the given indexing type.");
  static_assert(std::is_trivially_copyable<T>::value, "Type T must be trivially copyable.");

  Ringbuffer() noexcept {
    head_.value.store(0, std::memory_order_relaxed);
    tail_.value.store(0, std::memory_order_relaxed);
  }

  // Non-copyable, non-movable
  Ringbuffer(const Ringbuffer&) = delete;
  Ringbuffer& operator=(const Ringbuffer&) = delete;

  // ---- Producer API ----

  /// @brief Push one element by copy.
  /// @return true if successful, false if buffer is full.
  bool Push(const T& data) { return PushImpl(data); }

  /// @brief Push one element by move.
  /// @return true if successful, false if buffer is full.
  bool Push(T&& data) { return PushImpl(std::move(data)); }

  /// @brief Push one element produced by a callback, only if space is available.
  /// @tparam Callable  Any callable returning T (function pointer, lambda, std::function).
  /// @return true if callback was called and element was pushed.
  template <typename Callable>
  bool PushFromCallback(Callable&& callback) {
    const IndexT current_head = head_.value.load(std::memory_order_relaxed);
    const IndexT current_tail = tail_.value.load(AcquireOrder());

    if ((current_head - current_tail) == BufferSize) {
      return false;
    }

    data_buff_[current_head & kMask] = callback();
    head_.value.store(current_head + 1, ReleaseOrder());
    return true;
  }

  /// @brief Push multiple elements from a contiguous buffer.
  /// @return Number of elements actually pushed.
  std::size_t PushBatch(const T* buf, std::size_t count) {
    return PushBatchCore(buf, count);
  }

  /// @brief Push multiple elements with a notification callback after each batch.
  /// @tparam Callable  Any callable with signature void().
  /// @return Number of elements actually pushed.
  template <typename Callable>
  std::size_t PushBatch(const T* buf, std::size_t count, Callable&& callback) {
    return PushBatchCore(buf, count, &callback);
  }

  /// @brief Clear buffer from producer side (sets head = tail).
  /// @note Only call from the producer thread.
  void ProducerClear() noexcept {
    // Producer owns head_. Read tail and set head to match it.
    head_.value.store(tail_.value.load(std::memory_order_relaxed), std::memory_order_relaxed);
  }

  // ---- Consumer API ----

  /// @brief Pop one element from the buffer.
  /// @param[out] data  Where to store the popped element.
  /// @return true if successful, false if buffer is empty.
  bool Pop(T& data) {
    const IndexT current_tail = tail_.value.load(std::memory_order_relaxed);
    const IndexT current_head = head_.value.load(AcquireOrder());

    if (current_tail == current_head) {
      return false;
    }

    data = data_buff_[current_tail & kMask];
    tail_.value.store(current_tail + 1, ReleaseOrder());
    return true;
  }

  /// @brief Pop multiple elements into a contiguous buffer.
  /// @return Number of elements actually popped.
  std::size_t PopBatch(T* buf, std::size_t count) {
    return PopBatchCore(buf, count);
  }

  /// @brief Pop multiple elements with a notification callback after each batch.
  /// @tparam Callable  Any callable with signature void().
  /// @return Number of elements actually popped.
  template <typename Callable>
  std::size_t PopBatch(T* buf, std::size_t count, Callable&& callback) {
    return PopBatchCore(buf, count, &callback);
  }

  /// @brief Discard elements without reading them.
  /// @param count  Number of elements to discard (default 1).
  /// @return Number of elements actually discarded.
  std::size_t Discard(std::size_t count = 1) {
    const IndexT current_tail = tail_.value.load(std::memory_order_relaxed);
    const IndexT current_head = head_.value.load(std::memory_order_relaxed);
    const IndexT available = current_head - current_tail;
    const std::size_t to_discard = std::min(count, static_cast<std::size_t>(available));

    if (to_discard > 0) {
      tail_.value.store(current_tail + static_cast<IndexT>(to_discard), ReleaseOrder());
    }
    return to_discard;
  }

  /// @brief Peek at the front element without removing it.
  /// @return Pointer to the front element, or nullptr if empty.
  T* Peek() {
    const IndexT current_tail = tail_.value.load(std::memory_order_relaxed);
    const IndexT current_head = head_.value.load(AcquireOrder());

    if (current_tail == current_head) {
      return nullptr;
    }
    return &data_buff_[current_tail & kMask];
  }

  /// @brief Access the n-th element from the consumer side with bounds checking.
  /// @return Pointer to the element, or nullptr if index is out of range.
  T* At(std::size_t index) {
    const IndexT current_tail = tail_.value.load(std::memory_order_relaxed);
    const IndexT current_head = head_.value.load(AcquireOrder());

    if ((current_head - current_tail) <= static_cast<IndexT>(index)) {
      return nullptr;
    }
    return &data_buff_[(current_tail + index) & kMask];
  }

  /// @brief Access the n-th element without bounds checking.
  /// @warning Undefined behavior if index >= Size().
  const T& operator[](std::size_t index) const noexcept {
    return data_buff_[(tail_.value.load(std::memory_order_relaxed) + index) & kMask];
  }

  /// @brief Clear buffer from consumer side (sets tail = head).
  /// @note Only call from the consumer thread.
  void ConsumerClear() noexcept {
    tail_.value.store(head_.value.load(std::memory_order_relaxed), std::memory_order_relaxed);
  }

  // ---- Query API (either side) ----

  /// @brief Number of elements available to read.
  IndexT Size() const noexcept {
    return head_.value.load(AcquireOrder()) - tail_.value.load(std::memory_order_relaxed);
  }

  /// @brief Number of free slots available for writing.
  IndexT Available() const noexcept {
    return BufferSize - (head_.value.load(std::memory_order_relaxed) -
                         tail_.value.load(AcquireOrder()));
  }

  bool IsEmpty() const noexcept { return Size() == 0; }
  bool IsFull() const noexcept { return Available() == 0; }
  static constexpr std::size_t Capacity() noexcept { return BufferSize; }

 private:
  template <typename U>
  bool PushImpl(U&& data) {
    const IndexT current_head = head_.value.load(std::memory_order_relaxed);
    const IndexT current_tail = tail_.value.load(AcquireOrder());

    if ((current_head - current_tail) == BufferSize) {
      return false;
    }

    data_buff_[current_head & kMask] = std::forward<U>(data);
    head_.value.store(current_head + 1, ReleaseOrder());
    return true;
  }

  std::size_t PushBatchCore(const T* buf, std::size_t count) {
    std::size_t written = 0;
    IndexT current_head = head_.value.load(std::memory_order_relaxed);

    while (written < count) {
      const IndexT current_tail = tail_.value.load(AcquireOrder());
      const IndexT space = BufferSize - (current_head - current_tail);

      if (space == 0) {
        break;
      }

      const std::size_t to_write = std::min(count - written, static_cast<std::size_t>(space));
      const std::size_t head_offset = current_head & kMask;
      const std::size_t first_part = std::min(to_write, BufferSize - head_offset);

      std::memcpy(&data_buff_[head_offset], buf + written, first_part * sizeof(T));
      if (to_write > first_part) {
        std::memcpy(&data_buff_[0], buf + written + first_part, (to_write - first_part) * sizeof(T));
      }

      written += to_write;
      current_head += static_cast<IndexT>(to_write);
      head_.value.store(current_head, ReleaseOrder());
    }
    return written;
  }

  template <typename Callable>
  std::size_t PushBatchCore(const T* buf, std::size_t count, Callable* callback) {
    std::size_t written = 0;
    IndexT current_head = head_.value.load(std::memory_order_relaxed);

    while (written < count) {
      const IndexT current_tail = tail_.value.load(AcquireOrder());
      const IndexT space = BufferSize - (current_head - current_tail);

      if (space == 0) {
        break;
      }

      const std::size_t to_write = std::min(count - written, static_cast<std::size_t>(space));
      const std::size_t head_offset = current_head & kMask;
      const std::size_t first_part = std::min(to_write, BufferSize - head_offset);

      std::memcpy(&data_buff_[head_offset], buf + written, first_part * sizeof(T));
      if (to_write > first_part) {
        std::memcpy(&data_buff_[0], buf + written + first_part, (to_write - first_part) * sizeof(T));
      }

      written += to_write;
      current_head += static_cast<IndexT>(to_write);
      head_.value.store(current_head, ReleaseOrder());

      (*callback)();
    }
    return written;
  }

  std::size_t PopBatchCore(T* buf, std::size_t count) {
    std::size_t read = 0;
    IndexT current_tail = tail_.value.load(std::memory_order_relaxed);

    while (read < count) {
      const IndexT current_head = head_.value.load(AcquireOrder());
      const IndexT available = current_head - current_tail;

      if (available == 0) {
        break;
      }

      const std::size_t to_read = std::min(count - read, static_cast<std::size_t>(available));
      const std::size_t tail_offset = current_tail & kMask;
      const std::size_t first_part = std::min(to_read, BufferSize - tail_offset);

      std::memcpy(buf + read, &data_buff_[tail_offset], first_part * sizeof(T));
      if (to_read > first_part) {
        std::memcpy(buf + read + first_part, &data_buff_[0], (to_read - first_part) * sizeof(T));
      }

      read += to_read;
      current_tail += static_cast<IndexT>(to_read);
      tail_.value.store(current_tail, ReleaseOrder());
    }
    return read;
  }

  template <typename Callable>
  std::size_t PopBatchCore(T* buf, std::size_t count, Callable* callback) {
    std::size_t read = 0;
    IndexT current_tail = tail_.value.load(std::memory_order_relaxed);

    while (read < count) {
      const IndexT current_head = head_.value.load(AcquireOrder());
      const IndexT available = current_head - current_tail;

      if (available == 0) {
        break;
      }

      const std::size_t to_read = std::min(count - read, static_cast<std::size_t>(available));
      const std::size_t tail_offset = current_tail & kMask;
      const std::size_t first_part = std::min(to_read, BufferSize - tail_offset);

      std::memcpy(buf + read, &data_buff_[tail_offset], first_part * sizeof(T));
      if (to_read > first_part) {
        std::memcpy(buf + read + first_part, &data_buff_[0], (to_read - first_part) * sizeof(T));
      }

      read += to_read;
      current_tail += static_cast<IndexT>(to_read);
      tail_.value.store(current_tail, ReleaseOrder());

      (*callback)();
    }
    return read;
  }

  static constexpr std::memory_order AcquireOrder() noexcept {
    return FakeTSO ? std::memory_order_relaxed : std::memory_order_acquire;
  }

  static constexpr std::memory_order ReleaseOrder() noexcept {
    return FakeTSO ? std::memory_order_relaxed : std::memory_order_release;
  }

  static constexpr IndexT kMask = BufferSize - 1u;

  // Cache-line padded atomic indices to avoid false sharing
  struct alignas(64) PaddedIndex {
    std::atomic<IndexT> value{0};
    char padding[64 - sizeof(std::atomic<IndexT>)]{};
    static_assert(sizeof(std::atomic<IndexT>) <= 64, "Atomic index exceeds cache line size.");
  };

  PaddedIndex head_;                       // Producer writes, consumer reads
  PaddedIndex tail_;                       // Consumer writes, producer reads
  alignas(64) T data_buff_[BufferSize]{};  // Circular buffer storage
};

}  // namespace spsc

#endif  // SPSC_RINGBUFFER_HPP
