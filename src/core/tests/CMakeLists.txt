add_executable(test_task test_task.cpp)
target_link_libraries(test_task MusicStreamer::Core)
add_test(test_task test_task)

add_executable(test_io_context test_io_context.cpp)
target_link_libraries(test_io_context MusicStreamer::Core)
add_test(test_io_context test_io_context)

add_executable(test_sync_wait test_sync_wait.cpp)
target_link_libraries(test_sync_wait MusicStreamer::Core)
add_test(test_sync_wait test_sync_wait)

add_executable(test_async_queue test_async_queue.cpp)
target_link_libraries(test_async_queue MusicStreamer::Core)
add_test(test_async_queue test_async_queue)

add_executable(test_when_all test_when_all.cpp)
target_link_libraries(test_when_all MusicStreamer::Core)
add_test(test_when_all test_when_all)

add_executable(test_when_any test_when_any.cpp)
target_link_libraries(test_when_any MusicStreamer::Core)
add_test(test_when_any test_when_any)