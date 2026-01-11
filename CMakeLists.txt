cmake_minimum_required(VERSION 3.24)

project(CoroWayland LANGUAGES CXX)

option(CORO_WAYLAND_BUILD_TESTING "Build test executables" ON)
if (CORO_WAYLAND_BUILD_TESTING)
    enable_testing()
endif()

add_subdirectory(src)