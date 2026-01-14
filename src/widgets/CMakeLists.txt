add_library(CoroWayland_Widgets
    Text.cpp)
target_include_directories(CoroWayland_Widgets PUBLIC include)
target_link_libraries(CoroWayland_Widgets PUBLIC CoroWayland::Renderer)
add_library(CoroWayland::Widgets ALIAS CoroWayland_Widgets)