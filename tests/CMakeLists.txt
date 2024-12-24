include(FetchContent)
set(CATCH2_VERSION v2.13.10)
FetchContent_Declare(
  Catch2
  URL https://github.com/catchorg/Catch2/archive/refs/tags/${CATCH2_VERSION}.zip
  DOWNLOAD_EXTRACT_TIMESTAMP ON)
FetchContent_MakeAvailable(Catch2)

add_executable(ringbuffer_tests
    test_main.cpp
    test_push_pop.cpp
    test_batch.cpp
    test_query.cpp
    test_callback.cpp
    test_boundary.cpp
    test_spsc.cpp)
target_link_libraries(ringbuffer_tests Catch2::Catch2 ringbuffer Threads::Threads)
target_compile_options(ringbuffer_tests PRIVATE -Wall -Wextra -Wpedantic)

include(CTest)
add_test(NAME ringbuffer_tests COMMAND ringbuffer_tests)
