add_library(CoroWayland_JinjaTemplateEngine JinjaTemplateEngine.cpp)
add_library(CoroWayland::JinjaTemplateEngine ALIAS CoroWayland_JinjaTemplateEngine)
target_include_directories(CoroWayland_JinjaTemplateEngine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})


add_executable(code_generator 
    code_generator.cpp
    WaylandXmlParser.cpp)

target_link_libraries(code_generator CoroWayland::JinjaTemplateEngine)

if (MS_BUILD_TESTING)
    add_subdirectory(tests)
endif()