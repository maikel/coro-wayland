add_library(CoroWayland_Core
  AsyncScope.cpp
  FileDescriptor.cpp
  IoContext.cpp
  IoTask.cpp
  Strand.cpp
  Task.cpp
  write_env.cpp)
target_include_directories(CoroWayland_Core PUBLIC include)
add_library(CoroWayland::Core ALIAS CoroWayland_Core)

if (CORO_WAYLAND_BUILD_TESTING)
    add_subdirectory(tests)
endif()