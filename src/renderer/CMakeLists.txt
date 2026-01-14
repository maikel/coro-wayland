find_package(Freetype REQUIRED)

add_library(CoroWayland_Renderer STATIC
    Font.cpp
    GlyphCache.cpp
    PixelsView.cpp
    TextRenderer.cpp
)
add_library(CoroWayland::Renderer ALIAS CoroWayland_Renderer)
target_include_directories(CoroWayland_Renderer PUBLIC include)
target_link_libraries(CoroWayland_Renderer PUBLIC Freetype::Freetype CoroWayland::Core)

if (CORO_WAYLAND_BUILD_TESTING)
  add_subdirectory(tests)
endif()