add_library(MusicStreamer_JinjaTemplateEngine JinjaTemplateEngine.cpp)
add_library(MusicStreamer::JinjaTemplateEngine ALIAS MusicStreamer_JinjaTemplateEngine)
target_include_directories(MusicStreamer_JinjaTemplateEngine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})


add_executable(code_generator 
    code_generator.cpp
    WaylandXmlParser.cpp)

target_link_libraries(code_generator MusicStreamer::JinjaTemplateEngine)

if (MS_BUILD_TESTING)
    add_subdirectory(tests)
endif()