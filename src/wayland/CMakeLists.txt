add_custom_command(
  OUTPUT ${CMAKE_BINARY_DIR}/generated/include/wayland/protocol.hpp
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/generated/include/wayland
  COMMAND code_generator < ${CMAKE_CURRENT_SOURCE_DIR}/protocol.hpp.in > ${CMAKE_BINARY_DIR}/generated/include/wayland/protocol.hpp
  COMMAND clang-format-20 -i ${CMAKE_BINARY_DIR}/generated/include/wayland/protocol.hpp
  DEPENDS code_generator protocol.hpp.in
)

add_custom_target(RunGenerator DEPENDS ${CMAKE_BINARY_DIR}/generated/include/wayland/protocol.hpp)

add_library(MusicStreamer_Wayland Connection.cpp)
add_dependencies(MusicStreamer_Wayland RunGenerator)
target_include_directories(MusicStreamer_Wayland PUBLIC include ${CMAKE_BINARY_DIR}/generated/include)
target_link_libraries(MusicStreamer_Wayland PUBLIC MusicStreamer::Core MusicStreamer::logging)
add_library(MusicStreamer::Wayland ALIAS MusicStreamer_Wayland)

add_executable(wayland_app wayland_app.cpp)
target_link_libraries(wayland_app MusicStreamer::Wayland)