find_package(Freetype REQUIRED)

add_library(CoroWayland_Renderer STATIC
    FontManager.cpp
    Font.cpp
    GlyphCache.cpp
    TextRenderer.cpp
)
add_library(CoroWayland::Renderer ALIAS CoroWayland_Renderer)
target_include_directories(CoroWayland_Renderer PUBLIC include)
target_link_libraries(CoroWayland_Renderer PUBLIC Freetype::Freetype)