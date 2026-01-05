add_library(MusicStreamer_Wayland Connection.cpp)
target_include_directories(MusicStreamer_Wayland PUBLIC include)
target_link_libraries(MusicStreamer_Wayland PUBLIC MusicStreamer::Core MusicStreamer::logging)
add_library(MusicStreamer::Wayland ALIAS MusicStreamer_Wayland)

add_executable(wayland_app wayland_app.cpp)
target_link_libraries(wayland_app MusicStreamer::Wayland)