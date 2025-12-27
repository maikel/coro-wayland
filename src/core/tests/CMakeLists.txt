add_executable(test_task test_task.cpp)
target_link_libraries(test_task MusicStreamer::Core)
add_test(test_task test_task)

add_executable(test_io_context test_io_context.cpp)
target_link_libraries(test_io_context MusicStreamer::Core)
add_test(test_io_context test_io_context)