# Wayland Window Management Design Patterns

## Overview

This guide explains proper Wayland window management, focusing on the **XDG Shell protocol** which is the standard for desktop window management in Wayland.

## Key Concepts

### 1. **Window Lifecycle**

```
wl_surface → xdg_surface → xdg_toplevel
    ↓            ↓              ↓
  pixels     role mgmt    window behavior
```

- **wl_surface**: Raw drawing surface (holds pixels)
- **xdg_surface**: Adds role management and synchronization
- **xdg_toplevel**: Adds window semantics (title, minimize, maximize, etc.)

### 2. **Configure-Commit Protocol**

This is the **most important** pattern in Wayland:

```
Compositor              Client
    |                      |
    |--- configure ------->|  (suggests size/state)
    |                      |
    |                      |  Client prepares buffer
    |                      |  Client resizes content
    |                      |
    |<-- ack_configure ----|  (acknowledges serial)
    |<-- commit -----------|  (applies changes)
    |                      |
```

**Critical rules:**
1. Never commit before receiving first configure
2. Always ack_configure before commit
3. Use the serial from configure in ack_configure
4. xdg_surface.configure comes AFTER xdg_toplevel.configure

### 3. **Buffer Management & Viewport**

#### Surface vs Buffer Coordinates

- **Buffer pixels**: Actual pixel data (e.g., 3840×2160 on HiDPI)
- **Surface coordinates**: Logical units (e.g., 1920×1080)
- **Scale factor**: buffer_size / surface_size

```cpp
surface_scale = 2;  // HiDPI display
surface_size = 800×600;
buffer_size = 1600×1200;  // (800*2 × 600*2)
```

#### Viewport (wl_viewport protocol)

The viewport defines which region of the buffer is visible:

```
┌─────────────────┐
│   Buffer        │  Large buffer (e.g., 4096×4096)
│  ┌──────────┐   │
│  │ Viewport │   │  Visible region (e.g., 800×600)
│  │          │   │
│  └──────────┘   │
└─────────────────┘
```

**Use cases:**
- **Virtual desktop**: Pan around large canvas
- **Video playback**: Crop/scale efficiently
- **Document viewer**: Show portion of large document

## Design Patterns

### Pattern 1: Resizable Window with Buffer Pool

**Problem**: Constantly reallocating buffers on resize is expensive.

**Solution**: Over-allocate and reuse buffers.

```cpp
class BufferPool {
  // Allocate in power-of-2 increments
  void ensure_size(int width, int height) {
    size_t needed = width * height * 4;  // ARGB
    size_t allocated = next_power_of_2(needed * 2);  // Double buffer
    
    if (allocated > current_size) {
      reallocate(allocated);
    }
  }
};
```

**Key points:**
- Use memfd_create() for shared memory
- mmap() for efficient access
- Allocate 2× size for double buffering
- Only reallocate when exceeding current pool

### Pattern 2: State-Driven Rendering

**Problem**: When should you render?

**Solution**: Render only on state changes.

```cpp
class Window {
  enum class State {
    Uninitialized,    // Before first configure
    ConfigPending,    // Received configure, not acked
    Ready,            // Normal state
    Resizing          // Mid-resize (batch updates)
  };
  
  void handle_configure(int width, int height) {
    pending_width = width;
    pending_height = height;
    state = State::ConfigPending;
  }
  
  void handle_xdg_surface_configure(uint32_t serial) {
    ack_configure(serial);
    
    if (state == State::ConfigPending) {
      resize_buffers(pending_width, pending_height);
      render();
      commit();
      state = State::Ready;
    }
  }
};
```

**Benefits:**
- No rendering before first configure
- Batch multiple configure events
- Clear state transitions

### Pattern 3: Multi-Output Scaling

**Problem**: Window spans multiple monitors with different scale factors.

**Solution**: Render at highest scale, let compositor downscale.

```cpp
class Window {
  void handle_surface_enter(wl_output* output) {
    int scale = get_output_scale(output);
    max_scale = std::max(max_scale, scale);
    
    if (scale_changed()) {
      resize_buffers(width * max_scale, height * max_scale);
      surface.set_buffer_scale(max_scale);
    }
  }
  
  void handle_surface_leave(wl_output* output) {
    // Recalculate max_scale from remaining outputs
  }
};
```

**Rationale:**
- Prevents blurry rendering on HiDPI displays
- Compositor scales down efficiently
- Single buffer works across all outputs

### Pattern 4: Frame Scheduling

**Problem**: When to draw next frame?

**Solution**: Use frame callbacks for sync with compositor.

```cpp
// NOT AVAILABLE YET in your generated bindings, but pattern:
void render_loop() {
  while (running) {
    wl_surface_frame(surface, frame_callback);
    render();
    commit();
    wait_for_frame_callback();  // Blocks until vsync
  }
}
```

**For now, use timer-based approach:**
```cpp
while (running) {
  co_await scheduler.schedule_after(16ms);  // ~60fps
  if (needs_redraw) {
    render();
  }
}
```

### Pattern 5: Damage Tracking

**Problem**: Redrawing entire window is wasteful.

**Solution**: Track and report damaged regions.

```cpp
class Window {
  struct DirtyRegion {
    int x, y, width, height;
  };
  std::vector<DirtyRegion> dirty_regions;
  
  void invalidate(int x, int y, int w, int h) {
    dirty_regions.push_back({x, y, w, h});
    needs_redraw = true;
  }
  
  void render() {
    for (auto& region : dirty_regions) {
      render_region(region);
      surface.damage(region.x, region.y, region.width, region.height);
    }
    dirty_regions.clear();
    commit();
  }
};
```

**Benefits:**
- Compositor can optimize based on damage
- Reduces GPU workload
- Lower power consumption

## Common Pitfalls

### ❌ Pitfall 1: Committing Before First Configure

```cpp
// WRONG
surface = create_surface();
xdg_surface = create_xdg_surface(surface);
xdg_toplevel = xdg_surface.get_toplevel();
surface.commit();  // ❌ TOO EARLY!
```

**Why it fails**: Compositor doesn't know window size yet.

**Fix**: Wait for configure event first.

```cpp
// CORRECT
xdg_toplevel = xdg_surface.get_toplevel();
surface.commit();  // Initial commit to request configure
// ... wait for configure event ...
ack_configure(serial);
// ... resize buffers ...
commit();  // Now it's safe
```

### ❌ Pitfall 2: Ignoring Scale Factor

```cpp
// WRONG
wl_buffer* buffer = create_shm_buffer(800, 600);
surface.attach(buffer);
// ❌ Blurry on HiDPI!
```

**Fix**: Always account for scale.

```cpp
// CORRECT
int scale = output.get_scale();
wl_buffer* buffer = create_shm_buffer(800 * scale, 600 * scale);
surface.attach(buffer);
surface.set_buffer_scale(scale);
surface.damage(0, 0, 800, 600);  // In surface coordinates!
commit();
```

### ❌ Pitfall 3: Not Handling Size Hints

```cpp
// WRONG
void handle_configure(int width, int height) {
  // Always using suggested size
  resize_to(width, height);
}
```

**Fix**: Handle zero (compositor says "you choose").

```cpp
// CORRECT
void handle_configure(int width, int height) {
  if (width == 0 || height == 0) {
    // Use current size or default
    width = current_width ?: 800;
    height = current_height ?: 600;
  }
  resize_to(width, height);
}
```

### ❌ Pitfall 4: Forgetting Window Geometry

```cpp
// WRONG
// Never setting window geometry
// Compositor doesn't know window bounds!
```

**Fix**: Set geometry to match visible content.

```cpp
// CORRECT
void resize(int width, int height) {
  // ... resize buffers ...
  xdg_surface.set_window_geometry(0, 0, width, height);
  commit();
}
```

**Why it matters**: 
- Window managers need bounds for tiling
- Client-side decorations need proper geometry
- Maximized windows need exact size

## Advanced: Using Viewports

For applications with large buffers (e.g., maps, CAD):

```cpp
class LargeBufferWindow {
  // Full content buffer (very large)
  constexpr int BUFFER_WIDTH = 8192;
  constexpr int BUFFER_HEIGHT = 8192;
  
  // Visible window
  int viewport_x = 0;
  int viewport_y = 0;
  int viewport_width = 800;
  int viewport_height = 600;
  
  void pan(int dx, int dy) {
    viewport_x = std::clamp(viewport_x + dx, 0, BUFFER_WIDTH - viewport_width);
    viewport_y = std::clamp(viewport_y + dy, 0, BUFFER_HEIGHT - viewport_height);
    
    // Use wl_viewport (requires wp_viewporter global)
    viewport.set_source(
      wl_fixed_from_int(viewport_x),
      wl_fixed_from_int(viewport_y),
      wl_fixed_from_int(viewport_width),
      wl_fixed_from_int(viewport_height)
    );
    
    commit();  // No need to redraw buffer!
  }
};
```

**Benefits:**
- Pan without CPU rendering
- Zoom without resizing buffers
- Video playback with crop/scale

## State Machine Diagram

```
                    ┌──────────────┐
                    │ Uninitialized│
                    └───────┬──────┘
                            │ create_toplevel()
                            │ commit()
                            ▼
                    ┌──────────────┐
                    │  Waiting     │◄───────┐
                    │  Configure   │        │
                    └───────┬──────┘        │
                            │ configure     │
                            ▼               │
                    ┌──────────────┐        │
                    │  Preparing   │        │
                    │   Buffer     │        │
                    └───────┬──────┘        │
                            │ ack + commit  │
                            ▼               │
                    ┌──────────────┐        │
                    │    Ready     │────────┘
                    │   (mapped)   │ resize/state change
                    └──────────────┘
```

## Recommended Class Structure

```cpp
// Separate concerns for maintainability

class BufferPool {
  // Memory management
  // Allocation, resizing
  // mdspan views
};

class Surface {
  // Wayland surface operations
  // attach(), damage(), commit()
  // scale management
};

class Window {
  // High-level window behavior
  // Event handling
  // State machine
  
  BufferPool buffers;
  Surface surface;
};

class Renderer {
  // Drawing operations
  // Completely decoupled from Wayland
  void draw(std::mdspan<uint32_t, 2> framebuffer);
};
```

## Testing Checklist

✅ Window opens at reasonable default size
✅ Resize works smoothly (drag corner/edge)
✅ Maximize/unmaximize works
✅ Fullscreen/unfullscreen works
✅ Window moves between monitors with different scales
✅ Window renders correctly on HiDPI displays
✅ Close button works
✅ Responds to Alt+F4 or compositor close
✅ Min/max size constraints respected
✅ Window remembers size between runs (if desired)

## Performance Tips

1. **Double buffering**: Always use 2+ buffers to avoid tearing
2. **Buffer reuse**: Don't destroy/recreate on every frame
3. **Damage tracking**: Only damage changed regions
4. **Scale at allocation**: Allocate buffers at target scale
5. **Batch updates**: Handle multiple configure events before rendering
6. **Frame callbacks**: Sync with compositor (when available)

## Further Reading

- [Wayland Book - Chapter on XDG Shell](https://wayland-book.com/)
- [wayland-protocols: xdg-shell.xml](https://gitlab.freedesktop.org/wayland/wayland-protocols)
- [wlroots examples](https://github.com/swaywm/wlroots/tree/master/examples)

## Summary

**The golden rules:**
1. **Never commit before first configure**
2. **Always ack_configure before commit after configure**
3. **Handle scale factors for HiDPI**
4. **Set window_geometry to match content**
5. **Handle zero size (compositor says "you choose")**
6. **Use damage tracking for efficiency**
7. **Manage buffer pool to avoid constant reallocation**

Your current `wayland_app.cpp` is a good start but lacks:
- Proper resize handling (always 1920×1080)
- Scale factor support
- Dynamic buffer allocation
- Clean separation of concerns

The `window_example.cpp` demonstrates all these patterns properly!
