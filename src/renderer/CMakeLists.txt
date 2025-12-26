# Graphics Renderer Module

# Create the renderer library
add_library(MusicStreamer_Renderer Rasterizer.cpp)

target_include_directories(MusicStreamer_Renderer PUBLIC include)

# Add a test executable for the renderer
add_executable(graphics_demo demo/graphics_demo.cpp)
target_link_libraries(graphics_demo MusicStreamer_Renderer)
