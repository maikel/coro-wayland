add_executable(test_task test_task.cpp)
target_link_libraries(test_task CoroWayland::Core)
add_test(test_task test_task)

add_executable(test_io_context test_io_context.cpp)
target_link_libraries(test_io_context CoroWayland::Core)
add_test(test_io_context test_io_context)

add_executable(test_sync_wait test_sync_wait.cpp)
target_link_libraries(test_sync_wait CoroWayland::Core)
add_test(test_sync_wait test_sync_wait)

add_executable(test_async_queue test_async_queue.cpp)
target_link_libraries(test_async_queue CoroWayland::Core)
add_test(test_async_queue test_async_queue)

add_executable(test_when_all test_when_all.cpp)
target_link_libraries(test_when_all CoroWayland::Core)
add_test(test_when_all test_when_all)

add_executable(test_when_any test_when_any.cpp)
target_link_libraries(test_when_any CoroWayland::Core)
add_test(test_when_any test_when_any)

add_executable(test_observable test_observable.cpp)
target_link_libraries(test_observable CoroWayland::Core)
add_test(test_observable test_observable)

add_executable(test_async_scope test_async_scope.cpp)
target_link_libraries(test_async_scope CoroWayland::Core)
add_test(test_async_scope test_async_scope)

add_executable(test_coro_guard test_coro_guard.cpp)
target_link_libraries(test_coro_guard CoroWayland::Core)
add_test(test_coro_guard test_coro_guard)

add_executable(test_use_resource test_use_resource.cpp)
target_link_libraries(test_use_resource CoroWayland::Core)
add_test(test_use_resource test_use_resource)

add_executable(test_async_channel test_async_channel.cpp)
target_link_libraries(test_async_channel CoroWayland::Core)
add_test(test_async_channel test_async_channel)