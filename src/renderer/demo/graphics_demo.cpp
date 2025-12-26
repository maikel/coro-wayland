#include "../include/Graphics.hpp"
#include "../include/Rasterizer.hpp"
#include <iostream>
#include <fstream>
#include <format>
#include <chrono>

using namespace graphics;

/**
 * Simple PPM image format writer for visualizing our graphics
 */
void write_ppm(const std::string& filename, const PixelBufferOwned& buffer) {
    std::ofstream file(filename);
    if (!file) {
        std::cerr << "Failed to open " << filename << " for writing\n";
        return;
    }
    
    file << "P3\n";
    file << buffer.width() << " " << buffer.height() << "\n";
    file << "255\n";
    
    for (std::size_t y = 0; y < buffer.height(); ++y) {
        for (std::size_t x = 0; x < buffer.width(); ++x) {
            auto pixel = buffer.span()[x, y];
            Color color = Color::from_rgba(pixel);
            file << static_cast<int>(color.r) << " " 
                 << static_cast<int>(color.g) << " " 
                 << static_cast<int>(color.b) << " ";
        }
        file << "\n";
    }
    
    std::cout << "Saved image to " << filename << "\n";
}

/**
 * Test basic line drawing with different slopes and angles
 */
void test_basic_lines() {
    std::cout << "Testing basic line drawing...\n";
    
    PixelBufferOwned buffer(400, 300);
    buffer.clear(colors::BLACK);
    
    auto span = buffer.span();
    
    // Test horizontal line
    Rasterizer::draw_line_bresenham(span, {50, 50}, {350, 50}, colors::RED);
    
    // Test vertical line  
    Rasterizer::draw_line_bresenham(span, {100, 50}, {100, 250}, colors::GREEN);
    
    // Test diagonal line (45 degrees)
    Rasterizer::draw_line_bresenham(span, {150, 50}, {300, 200}, colors::BLUE);
    
    // Test steep line
    Rasterizer::draw_line_bresenham(span, {200, 50}, {220, 250}, colors::YELLOW);
    
    // Test shallow line
    Rasterizer::draw_line_bresenham(span, {250, 100}, {350, 120}, colors::CYAN);
    
    // Test negative slope
    Rasterizer::draw_line_bresenham(span, {300, 50}, {200, 150}, colors::MAGENTA);
    
    write_ppm("basic_lines.ppm", buffer);
}

/**
 * Test drawing patterns and shapes using lines
 */
void test_patterns() {
    std::cout << "Testing patterns and shapes...\n";
    
    PixelBufferOwned buffer(400, 400);
    buffer.clear(colors::BLACK);
    auto span = buffer.span();
    
    // Draw a star pattern
    Point center{200, 200};
    int radius = 80;
    
    for (int i = 0; i < 8; ++i) {
        double angle = i * M_PI / 4.0;
        Point end{
            center.x + static_cast<int>(radius * std::cos(angle)),
            center.y + static_cast<int>(radius * std::sin(angle))
        };
        Rasterizer::draw_line_bresenham(span, center, end, colors::WHITE);
    }
    
    // Draw concentric rectangles
    for (int i = 1; i <= 5; ++i) {
        int size = i * 15;
        Point top_left{center.x - size, center.y - size};
        Point bottom_right{center.x + size, center.y + size};
        Color rect_color{static_cast<std::uint8_t>(50 * i), 
                        static_cast<std::uint8_t>(255 - 30 * i), 
                        static_cast<std::uint8_t>(100 + 20 * i)};
        Rasterizer::draw_rectangle(span, top_left, bottom_right, rect_color);
    }
    
    write_ppm("patterns.ppm", buffer);
}

/**
 * Test thick lines
 */
void test_thick_lines() {
    std::cout << "Testing thick line drawing...\n";
    
    PixelBufferOwned buffer(400, 300);
    buffer.clear(colors::BLACK);
    auto span = buffer.span();
    
    // Draw lines of increasing thickness
    for (int thickness = 1; thickness <= 10; ++thickness) {
        int y_offset = thickness * 25;
        Rasterizer::draw_thick_line(span, 
                                   {50, y_offset}, 
                                   {350, y_offset}, 
                                   thickness, 
                                   colors::GREEN);
    }
    
    write_ppm("thick_lines.ppm", buffer);
}

/**
 * Performance test for Bresenham's algorithm
 */
void performance_test() {
    std::cout << "Running performance test...\n";
    
    PixelBufferOwned buffer(1920, 1080);
    buffer.clear(colors::BLACK);
    auto span = buffer.span();
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Draw many random lines
    const int num_lines = 10000;
    for (int i = 0; i < num_lines; ++i) {
        Point start{
            static_cast<int>(rand() % buffer.width()),
            static_cast<int>(rand() % buffer.height())
        };
        Point end{
            static_cast<int>(rand() % buffer.width()),
            static_cast<int>(rand() % buffer.height())
        };
        Color color{
            static_cast<std::uint8_t>(rand() % 256),
            static_cast<std::uint8_t>(rand() % 256),
            static_cast<std::uint8_t>(rand() % 256)
        };
        
        Rasterizer::draw_line_bresenham(span, start, end, color);
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    std::cout << std::format("Drew {} lines in {}ms ({:.2f} lines/ms)\n", 
                            num_lines, duration.count(), 
                            static_cast<double>(num_lines) / duration.count());
    
    write_ppm("performance_test.ppm", buffer);
}

/**
 * Test filled shapes
 */
void test_filled_shapes() {
    std::cout << "Testing filled shapes...\n";
    
    PixelBufferOwned buffer(400, 400);
    buffer.clear(colors::BLACK);
    auto span = buffer.span();
    
    // Draw some filled rectangles
    Rasterizer::fill_rectangle(span, {50, 50}, {150, 100}, colors::RED);
    Rasterizer::fill_rectangle(span, {200, 80}, {350, 150}, colors::GREEN);
    Rasterizer::fill_rectangle(span, {100, 200}, {300, 350}, colors::BLUE);
    
    // Overlay with some line patterns
    for (int i = 0; i < 10; ++i) {
        Rasterizer::draw_line_bresenham(span, {0, i * 40}, {400, i * 40}, colors::WHITE);
        Rasterizer::draw_line_bresenham(span, {i * 40, 0}, {i * 40, 400}, colors::WHITE);
    }
    
    write_ppm("filled_shapes.ppm", buffer);
}

int main() {
    std::cout << "Graphics Rasterization Demo\n";
    std::cout << "===========================\n\n";
    
    try {
        test_basic_lines();
        test_patterns();
        test_thick_lines();
        test_filled_shapes();
        performance_test();
        
        std::cout << "\nAll tests completed successfully!\n";
        std::cout << "Generated PPM files can be viewed with image viewers or converted to other formats.\n";
        std::cout << "For example: 'convert basic_lines.ppm basic_lines.png' (requires ImageMagick)\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}