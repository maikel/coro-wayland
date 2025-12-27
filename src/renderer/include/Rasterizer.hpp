#pragma once

#include "Graphics.hpp"
#include <algorithm>
#include <cmath>

namespace graphics {

/**
 * Graphics rasterization algorithms
 */
class Rasterizer {
public:
  /**
   * Draw a line using Bresenham's line algorithm
   *
   * This is the classic integer-only line drawing algorithm that's fast
   * and produces high-quality lines without floating-point operations.
   *
   * @param buffer The pixel buffer to draw on (any type satisfying PixelBuffer concept)
   * @param start Starting point of the line
   * @param end Ending point of the line
   * @param color Color to draw the line with
   */
  template <PixelBuffer Buffer>
  static void draw_line_bresenham(Buffer& buffer, Point start, Point end, Color color) {
    const auto width = static_cast<int>(buffer.extent(0));
    const auto height = static_cast<int>(buffer.extent(1));
    const auto pixel_value = color.to_rgba();

    int x0 = start.x, y0 = start.y;
    int x1 = end.x, y1 = end.y;

    // Calculate deltas
    int dx = std::abs(x1 - x0);
    int dy = std::abs(y1 - y0);

    // Determine direction of line
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;

    int err = dx - dy;
    int x = x0, y = y0;

    while (true) {
      // Plot point if within bounds
      if (x >= 0 && x < width && y >= 0 && y < height) {
        buffer[x, y] = pixel_value;
      }

      // Check if we've reached the end point
      if (x == x1 && y == y1)
        break;

      // Calculate error and adjust coordinates
      int e2 = 2 * err;

      if (e2 > -dy) {
        err -= dy;
        x += sx;
      }

      if (e2 < dx) {
        err += dx;
        y += sy;
      }
    }
  }

  /**
   * Draw a thick line using Bresenham's algorithm with width
   *
   * @param buffer The pixel buffer to draw on
   * @param start Starting point of the line
   * @param end Ending point of the line
   * @param width Line width in pixels
   * @param color Color to draw the line with
   */
  template <PixelBuffer Buffer>
  static void draw_thick_line(Buffer& buffer, Point start, Point end, int width, Color color) {
    if (width <= 1) {
      draw_line_bresenham(buffer, start, end, color);
      return;
    }

    // For thick lines, we'll draw multiple parallel lines
    const float half_width = width / 2.0f;

    // Calculate perpendicular vector
    Point delta = end - start;
    float length = std::sqrt(delta.x * delta.x + delta.y * delta.y);

    if (length == 0)
      return; // Degenerate line

    // Normalize perpendicular vector
    float perp_x = -delta.y / length;
    float perp_y = delta.x / length;

    // Draw multiple lines offset by the perpendicular vector
    for (int i = -static_cast<int>(half_width); i <= static_cast<int>(half_width); ++i) {
      Point offset_start{start.x + static_cast<int>(i * perp_x),
                         start.y + static_cast<int>(i * perp_y)};
      Point offset_end{end.x + static_cast<int>(i * perp_x), end.y + static_cast<int>(i * perp_y)};

      draw_line_bresenham(buffer, offset_start, offset_end, color);
    }
  }

  /**
   * Draw a rectangle outline
   *
   * @param buffer The pixel buffer to draw on
   * @param top_left Top-left corner of rectangle
   * @param bottom_right Bottom-right corner of rectangle
   * @param color Color to draw with
   */
  template <PixelBuffer Buffer>
  static void draw_rectangle(Buffer& buffer, Point top_left, Point bottom_right, Color color) {
    Point top_right{bottom_right.x, top_left.y};
    Point bottom_left{top_left.x, bottom_right.y};

    // Draw four sides
    draw_line_bresenham(buffer, top_left, top_right, color);       // Top
    draw_line_bresenham(buffer, top_right, bottom_right, color);   // Right
    draw_line_bresenham(buffer, bottom_right, bottom_left, color); // Bottom
    draw_line_bresenham(buffer, bottom_left, top_left, color);     // Left
  }

  /**
   * Fill a rectangle with solid color
   *
   * @param buffer The pixel buffer to draw on
   * @param top_left Top-left corner of rectangle
   * @param bottom_right Bottom-right corner of rectangle
   * @param color Fill color
   */
  template <PixelBuffer Buffer>
  static void fill_rectangle(Buffer& buffer, Point top_left, Point bottom_right, Color color) {
    const auto width = static_cast<int>(buffer.extent(0));
    const auto height = static_cast<int>(buffer.extent(1));
    const auto pixel_value = color.to_rgba();

    // Ensure coordinates are in correct order
    int x1 = std::min(top_left.x, bottom_right.x);
    int y1 = std::min(top_left.y, bottom_right.y);
    int x2 = std::max(top_left.x, bottom_right.x);
    int y2 = std::max(top_left.y, bottom_right.y);

    // Clamp to buffer bounds
    x1 = std::max(0, x1);
    y1 = std::max(0, y1);
    x2 = std::min(width - 1, x2);
    y2 = std::min(height - 1, y2);

    // Fill the rectangle
    for (int y = y1; y <= y2; ++y) {
      for (int x = x1; x <= x2; ++x) {
        buffer[x, y] = pixel_value;
      }
    }
  }

  /**
   * Draw a single pixel
   *
   * @param buffer The pixel buffer to draw on
   * @param point Position to draw at
   * @param color Color to draw with
   */
  template <PixelBuffer Buffer> static void draw_pixel(Buffer& buffer, Point point, Color color) {
    const auto width = static_cast<int>(buffer.extent(0));
    const auto height = static_cast<int>(buffer.extent(1));

    if (point.x >= 0 && point.x < width && point.y >= 0 && point.y < height) {
      buffer[point.x, point.y] = color.to_rgba();
    }
  }
};

} // namespace graphics