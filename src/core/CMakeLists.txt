add_library(MusicStreamer_Core
  AsyncScope.cpp
  FileDescriptor.cpp
  IoContext.cpp
  IoTask.cpp
  Strand.cpp
  Task.cpp
  write_env.cpp)
target_include_directories(MusicStreamer_Core PUBLIC include)
add_library(MusicStreamer::Core ALIAS MusicStreamer_Core)

if (MS_BUILD_TESTING)
    add_subdirectory(tests)
endif()