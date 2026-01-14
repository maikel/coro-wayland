find_file(WAYLAND_XML wayland.xml PATHS /usr/share/wayland)
find_file(XDG_SHELL_XML xdg-shell.xml PATHS /usr/share/wayland-protocols/stable/xdg-shell)

if (NOT WAYLAND_XML)
  message(FATAL_ERROR "Could not find wayland.xml")
endif()

if (NOT XDG_SHELL_XML)
  message(FATAL_ERROR "Could not find xdg-shell.xml")
endif()

message(STATUS "Using Wayland XML: ${WAYLAND_XML}")
message(STATUS "Using XDG Shell XML: ${XDG_SHELL_XML}")

add_custom_command(
  OUTPUT
    ${CMAKE_BINARY_DIR}/generated/include/wayland/protocol.hpp
    ${CMAKE_BINARY_DIR}/generated/protocol.cpp
    ${CMAKE_BINARY_DIR}/generated/include/wayland/XdgShell.hpp
    ${CMAKE_BINARY_DIR}/generated/XdgShell.cpp
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/generated/include/wayland
  COMMAND code_generator -i ${WAYLAND_XML} < ${CMAKE_CURRENT_SOURCE_DIR}/protocol.hpp.in | clang-format-20  > ${CMAKE_BINARY_DIR}/generated/include/wayland/protocol.hpp
  COMMAND code_generator -i ${WAYLAND_XML} < ${CMAKE_CURRENT_SOURCE_DIR}/protocol.cpp.in | clang-format-20 > ${CMAKE_BINARY_DIR}/generated/protocol.cpp
  COMMAND code_generator -i ${XDG_SHELL_XML} -e XdgShell < ${CMAKE_CURRENT_SOURCE_DIR}/protocol.hpp.in | clang-format-20  > ${CMAKE_BINARY_DIR}/generated/include/wayland/XdgShell.hpp
  COMMAND code_generator -i ${XDG_SHELL_XML} -e XdgShell < ${CMAKE_CURRENT_SOURCE_DIR}/protocol.cpp.in | clang-format-20 > ${CMAKE_BINARY_DIR}/generated/XdgShell.cpp
  DEPENDS code_generator protocol.hpp.in protocol.cpp.in
)

add_library(CoroWayland_Wayland
  Connection.cpp
  Client.cpp
  FrameBufferPool.cpp
  Window.cpp
  WindowSurface.cpp
  ${CMAKE_BINARY_DIR}/generated/protocol.cpp
  ${CMAKE_BINARY_DIR}/generated/XdgShell.cpp)
target_include_directories(CoroWayland_Wayland PUBLIC include ${CMAKE_BINARY_DIR}/generated/include)
target_link_libraries(CoroWayland_Wayland PUBLIC CoroWayland::Core CoroWayland::logging CoroWayland::Renderer CoroWayland::Widgets)
add_library(CoroWayland::Wayland ALIAS CoroWayland_Wayland)

add_executable(wayland_app wayland_app.cpp)
target_link_libraries(wayland_app CoroWayland::Wayland)
