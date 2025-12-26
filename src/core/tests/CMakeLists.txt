add_executable(test_task test_task.cpp)
target_link_libraries(test_task MusicStreamer::Core)

add_test(test_task test_task)