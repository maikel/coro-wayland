cmake_minimum_required(VERSION 3.24)

project(MusicStreamer LANGUAGES CXX)

option(MS_BUILD_TESTING "Build test executables" ON)
if (MS_BUILD_TESTING)
    enable_testing()
endif()

add_subdirectory(src)