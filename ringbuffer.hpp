#ifndef RINGBUFFER_HPP
#define RINGBUFFER_HPP

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>
namespace utility {

/*!
 * \brief Lock-free, no-wasted-slots ring buffer implementation.
 *
 * \tparam T Type of buffered elements.
 * \tparam BufferSize Size of the buffer. Must be a power of 2.
 * \tparam FakeTSO Omit generation of explicit barrier code to avoid unnecessary instructions in TSO scenario (e.g.,
 * simple microcontrollers/single core). \tparam IndexT Type of array indexing type. Serves also as placeholder for
 * future implementations.
 */
template <typename T, std::size_t BufferSize = 16, bool FakeTSO = false, typename IndexT = std::size_t>
class Ringbuffer {
 public:
  // Ensure buffer size is a power of 2 and other static assertions
  static_assert(BufferSize != 0, "Buffer size cannot be zero.");
  static_assert((BufferSize & (BufferSize - 1)) == 0, "Buffer size must be a power of 2.");
  static_assert(sizeof(IndexT) <= sizeof(std::size_t), "Index type size must not exceed size_t.");
  static_assert(std::is_unsigned<IndexT>::value, "Index type must be unsigned.");
  static_assert(BufferSize <= ((std::numeric_limits<IndexT>::max)() >> 1),
                "Buffer size is too large for the given indexing type.");
  static_assert(std::is_trivially_copyable<T>::value, "Type T must be trivially copyable.");

  /*!
   * \brief Default constructor, initializes head and tail indices.
   */
  Ringbuffer() noexcept {
    head_.value.store(0, std::memory_order_relaxed);
    tail_.value.store(0, std::memory_order_relaxed);
  }

  /*!
   * \brief Clear buffer from producer side.
   * \warning May return without performing any action if consumer tries to read data simultaneously.
   */
  void ProducerClear() noexcept {
    ConsumerClear();
  }

  /*!
   * \brief Clear buffer from consumer side.
   */
  void ConsumerClear() noexcept {
    tail_.value.store(head_.value.load(std::memory_order_relaxed), std::memory_order_relaxed);
  }

  /*!
   * \brief Check if buffer is empty.
   * \return True if buffer is empty.
   */
  bool IsEmpty() const noexcept {
    return ReadAvailable() == 0;
  }

  /*!
   * \brief Check if buffer is full.
   * \return True if buffer is full.
   */
  bool IsFull() const noexcept {
    return WriteAvailable() == 0;
  }

  /*!
   * \brief Check how many elements can be read from the buffer.
   * \return Number of elements that can be read.
   */
  IndexT ReadAvailable() const noexcept {
    const IndexT current_head = head_.value.load(GetAcquireMemoryOrder());
    const IndexT current_tail = tail_.value.load(std::memory_order_relaxed);
    return current_head - current_tail;
  }

  /*!
   * \brief Check how many elements can be written into the buffer.
   * \return Number of free slots available for writing.
   */
  IndexT WriteAvailable() const noexcept {
    const IndexT current_head = head_.value.load(std::memory_order_relaxed);
    const IndexT current_tail = tail_.value.load(GetAcquireMemoryOrder());
    return BufferSize - (current_head - current_tail);
  }

  /*!
   * \brief Insert data into the internal buffer without blocking.
   * \param data Element to be inserted.
   * \return True if data was inserted.
   */
  bool Insert(const T& data) {
    return InsertImpl(data);
  }

  /*!
   * \brief Insert data via pointer into the internal buffer without blocking.
   * \param data Pointer to the element to be inserted.
   * \return True if data was inserted.
   */
  bool Insert(const T* data) {
    return InsertImpl(*data);
  }

  /*!
   * \brief Insert data returned by a callback function into the internal buffer without blocking.
   *
   * \param get_data_callback Callback function that returns the element to be inserted.
   * \return True if data was inserted and callback was called.
   */
  bool InsertFromCallbackWhenAvailable(T (*get_data_callback)(void)) {
    const IndexT current_head = head_.value.load(std::memory_order_relaxed);
    const IndexT current_tail = tail_.value.load(GetAcquireMemoryOrder());

    if ((current_head - current_tail) == BufferSize) {
      return false;
    }

    data_buff_[current_head & buffer_mask_] = get_data_callback();
    std::atomic_thread_fence(std::memory_order_release);
    head_.value.store(current_head + 1, GetReleaseMemoryOrder());
    return true;
  }

  /*!
   * \brief Remove a single element without reading.
   * \return True if one element was removed.
   */
  bool Remove() {
    IndexT current_tail = tail_.value.load(std::memory_order_relaxed);
    const IndexT current_head = head_.value.load(std::memory_order_relaxed);

    if (current_tail == current_head) {
      return false;
    }

    tail_.value.store(current_tail + 1, GetReleaseMemoryOrder());
    return true;
  }

  /*!
   * \brief Remove multiple elements without reading.
   * \param cnt Maximum number of elements to remove.
   * \return Number of elements removed.
   */
  std::size_t Remove(std::size_t cnt) {
    const IndexT current_tail = tail_.value.load(std::memory_order_relaxed);
    const IndexT current_head = head_.value.load(std::memory_order_relaxed);
    const IndexT available = current_head - current_tail;
    const std::size_t to_remove = std::min(cnt, static_cast<std::size_t>(available));

    if (to_remove > 0) {
      tail_.value.store(current_tail + static_cast<IndexT>(to_remove), GetReleaseMemoryOrder());
    }

    return to_remove;
  }

  /*!
   * \brief Read one element from the internal buffer without blocking.
   * \param data Reference to where the removed element will be stored.
   * \return True if data was fetched from the internal buffer.
   */
  bool Remove(T& data) {
    return Remove(&data);
  }

  /*!
   * \brief Read one element from the internal buffer without blocking.
   * \param data Pointer to where the removed element will be stored.
   * \return True if data was fetched from the internal buffer.
   */
  bool Remove(T* data) {
    const IndexT current_tail = tail_.value.load(std::memory_order_relaxed);
    const IndexT current_head = head_.value.load(GetAcquireMemoryOrder());

    if (current_tail == current_head) {
      return false;
    }

    *data = data_buff_[current_tail & buffer_mask_];
    std::atomic_thread_fence(std::memory_order_release);
    tail_.value.store(current_tail + 1, GetReleaseMemoryOrder());
    return true;
  }

  /*!
   * \brief Get the first element in the buffer on the consumer side.
   * \return Pointer to the first element, or nullptr if the buffer is empty.
   */
  T* Peek() {
    const IndexT current_tail = tail_.value.load(std::memory_order_relaxed);
    const IndexT current_head = head_.value.load(GetAcquireMemoryOrder());

    if (current_tail == current_head) {
      return nullptr;
    }

    return &data_buff_[current_tail & buffer_mask_];
  }

  /*!
   * \brief Get the n'th element on the consumer side.
   * \param index Item offset starting on the consumer side.
   * \return Pointer to the requested element, or nullptr if the index exceeds the storage count.
   */
  T* At(std::size_t index) {
    const IndexT current_tail = tail_.value.load(std::memory_order_relaxed);
    const IndexT current_head = head_.value.load(GetAcquireMemoryOrder());

    if ((current_head - current_tail) <= static_cast<IndexT>(index)) {
      return nullptr;
    }

    return &data_buff_[(current_tail + index) & buffer_mask_];
  }

  /*!
   * \brief Get the n'th element on the consumer side without bounds checking.
   * \param index Item offset starting on the consumer side.
   * \return Reference to the requested element. Undefined behavior if index exceeds storage count.
   */
  T& operator[](std::size_t index) noexcept {
    return data_buff_[(tail_.value.load(std::memory_order_relaxed) + index) & buffer_mask_];
  }

  /*!
   * \brief Insert multiple elements into the internal buffer without blocking.
   *
   * This function inserts as much data as possible from the given buffer.
   *
   * \param buff Pointer to the buffer with data to be inserted.
   * \param count Number of elements to write from the given buffer.
   * \return Number of elements written into the internal buffer.
   */
  std::size_t WriteBuff(const T* buff, std::size_t count) {
    return WriteBuffImpl(buff, count, nullptr, 0);
  }

  /*!
   * \brief Insert multiple elements into the internal buffer without blocking.
   *
   * This function continues writing new entries until all data is written or there is no more space.
   * The callback function can be used to indicate to the consumer that it can start fetching data.
   *
   * \warning This function is not deterministic.
   *
   * \param buff Pointer to the buffer with data to be inserted.
   * \param count Number of elements to write from the given buffer.
   * \param count_to_callback Number of elements to write before calling the callback function in the first loop.
   * \param execute_data_callback Pointer to the callback function executed after every loop iteration.
   * \return Number of elements written into the internal buffer.
   */
  std::size_t WriteBuff(const T* buff, std::size_t count, std::size_t count_to_callback,
                        void (*execute_data_callback)(void)) {
    return WriteBuffImpl(buff, count, execute_data_callback, count_to_callback);
  }

  /*!
   * \brief Load multiple elements from the internal buffer without blocking.
   *
   * This function reads up to the specified amount of data.
   *
   * \param buff Pointer to the buffer where data will be loaded.
   * \param count Number of elements to load into the given buffer.
   * \return Number of elements that were read from the internal buffer.
   */
  std::size_t ReadBuff(T* buff, std::size_t count) {
    return ReadBuffImpl(buff, count, nullptr, 0);
  }

  /*!
   * \brief Load multiple elements from the internal buffer without blocking.
   *
   * This function continues reading new entries until all requested data is read or there is nothing more to read.
   * The callback function can be used to indicate to the producer that it can start writing new data.
   *
   * \warning This function is not deterministic.
   *
   * \param buff Pointer to the buffer where data will be loaded.
   * \param count Number of elements to load into the given buffer.
   * \param count_to_callback Number of elements to load before calling the callback function in the first iteration.
   * \param execute_data_callback Pointer to the callback function executed after every loop iteration.
   * \return Number of elements that were read from the internal buffer.
   */
  std::size_t ReadBuff(T* buff, std::size_t count, std::size_t count_to_callback, void (*execute_data_callback)(void)) {
    return ReadBuffImpl(buff, count, execute_data_callback, count_to_callback);
  }

 private:
  // Disable copy and assignment
  Ringbuffer(const Ringbuffer&) = delete;
  Ringbuffer& operator=(const Ringbuffer&) = delete;

  /*!
   * \brief Internal helper for inserting data.
   * \tparam U Type of the data to insert.
   * \param data Data to insert.
   * \return True if data was inserted.
   */
  template <typename U>
  bool InsertImpl(U&& data) {
    const IndexT current_head = head_.value.load(std::memory_order_relaxed);
    const IndexT current_tail = tail_.value.load(GetAcquireMemoryOrder());

    if ((current_head - current_tail) == BufferSize) {
      return false;
    }

    data_buff_[current_head & buffer_mask_] = std::forward<U>(data);
    std::atomic_thread_fence(std::memory_order_release);
    head_.value.store(current_head + 1, GetReleaseMemoryOrder());
    return true;
  }

  /*!
   * \brief Internal helper for writing multiple elements.
   * \param buff Pointer to the buffer with data to be inserted.
   * \param count Number of elements to write from the given buffer.
   * \param execute_data_callback Callback function to execute after each batch write.
   * \param count_to_callback Number of elements to write before calling the callback.
   * \return Number of elements written.
   */
  std::size_t WriteBuffImpl(const T* buff, std::size_t count, void (*execute_data_callback)(void),
                            std::size_t count_to_callback) {
    std::size_t written = 0;
    IndexT current_head = head_.value.load(std::memory_order_relaxed);

    while (written < count) {
      const IndexT current_tail = tail_.value.load(GetAcquireMemoryOrder());
      const IndexT available = BufferSize - (current_head - current_tail);

      if (available == 0) {
        break;
      }

      const std::size_t to_write = std::min(count - written, static_cast<std::size_t>(available));

      const std::size_t first_part =
          std::min(to_write, static_cast<std::size_t>(BufferSize - (current_head & buffer_mask_)));
      std::memcpy(&data_buff_[current_head & buffer_mask_], buff + written, first_part * sizeof(T));

      if (to_write > first_part) {
        std::memcpy(&data_buff_[0], buff + written + first_part, (to_write - first_part) * sizeof(T));
      }

      written += to_write;
      current_head += static_cast<IndexT>(to_write);
      std::atomic_thread_fence(std::memory_order_release);
      head_.value.store(current_head, GetReleaseMemoryOrder());

      if (execute_data_callback != nullptr) {
        execute_data_callback();
        if (count_to_callback != 0 && written >= count_to_callback) {
          break;
        }
      }
    }

    return written;
  }

  /*!
   * \brief Internal helper for reading multiple elements.
   * \param buff Pointer to the buffer where data will be loaded.
   * \param count Number of elements to load into the given buffer.
   * \param execute_data_callback Callback function to execute after each batch read.
   * \param count_to_callback Number of elements to load before calling the callback.
   * \return Number of elements read.
   */
  std::size_t ReadBuffImpl(T* buff, std::size_t count, void (*execute_data_callback)(void),
                           std::size_t count_to_callback) {
    std::size_t read = 0;
    IndexT current_tail = tail_.value.load(std::memory_order_relaxed);

    while (read < count) {
      const IndexT current_head = head_.value.load(GetAcquireMemoryOrder());
      const IndexT available = current_head - current_tail;

      if (available == 0) {
        break;
      }

      const std::size_t to_read = std::min(count - read, static_cast<std::size_t>(available));

      const std::size_t first_part =
          std::min(to_read, static_cast<std::size_t>(BufferSize - (current_tail & buffer_mask_)));
      std::memcpy(buff + read, &data_buff_[current_tail & buffer_mask_], first_part * sizeof(T));

      if (to_read > first_part) {
        std::memcpy(buff + read + first_part, &data_buff_[0], (to_read - first_part) * sizeof(T));
      }

      read += to_read;
      current_tail += static_cast<IndexT>(to_read);
      std::atomic_thread_fence(std::memory_order_release);
      tail_.value.store(current_tail, GetReleaseMemoryOrder());

      if (execute_data_callback != nullptr) {
        execute_data_callback();
        if (count_to_callback != 0 && read >= count_to_callback) {
          break;
        }
      }
    }

    return read;
  }

  /*!
   * \brief Get the appropriate acquire memory order based on FakeTSO.
   * \return Memory order for acquire operations.
   */
  constexpr std::memory_order GetAcquireMemoryOrder() const noexcept {
    return FakeTSO ? std::memory_order_relaxed : std::memory_order_acquire;
  }

  /*!
   * \brief Get the appropriate release memory order based on FakeTSO.
   * \return Memory order for release operations.
   */
  constexpr std::memory_order GetReleaseMemoryOrder() const noexcept {
    return FakeTSO ? std::memory_order_relaxed : std::memory_order_release;
  }

  // Constants
  static constexpr IndexT buffer_mask_ = BufferSize - 1u;

  // Padding structure to avoid false sharing
  struct alignas(64) PaddedAtomicIndex {
    std::atomic<IndexT> value;
    char padding[64 - sizeof(std::atomic<IndexT>)];

    // Constructor to initialize the atomic variable
    PaddedAtomicIndex() : value(0), padding{0} {
      static_assert(sizeof(padding) >= 64 - sizeof(std::atomic<IndexT>), "Padding size is insufficient.");
    }
  };

  // Member variables
  PaddedAtomicIndex head_;  // Head index
  PaddedAtomicIndex tail_;  // Tail index

  alignas(64) T data_buff_[BufferSize];  // Circular buffer storage
};

}  // namespace utility

#endif  // RINGBUFFER_HPP
