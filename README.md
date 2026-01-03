# MusicStreamer

A C++ project exploring the combination of reactive programming paradigms with Wayland client library development, built on top of a custom coroutine-based async runtime.

## Project Status

This project is in early development. The current implementation includes foundational infrastructure for async I/O, code generation tooling, and initial Wayland protocol bindings.

## Components

### Core Async Runtime

Located in `src/core/`, this provides a coroutine-based asynchronous execution framework:

- **Task<T>**: Lazily-evaluated coroutine tasks with customizable traits for cancellation and environment propagation
- **IoContext**: Single-threaded event loop managing immediate tasks, timers, and file descriptor polling with io_uring-style architecture
- **IoTask**: Awaitable operations for async I/O (timers, file descriptors, immediate yields)
- **sync_wait**: Synchronous entry point for coroutine execution

The runtime supports:
- Structured concurrency via stop_token cancellation
- Environment-based query system for passing contextual information through coroutine chains
- Thread-safe task enqueuing with single-threaded execution model

### Code Generator

Located in `src/code_generator/`, this implements a custom code generator for Wayland protocol definitions:

- **WaylandXmlParser**: XML parser specifically designed for Wayland protocol files
- **JinjaTemplateEngine**: Jinja2-style template engine supporting:
  - Variable substitution with nested object/array access (`{{ user.name }}`, `{{ items[0] }}`)
  - Conditional blocks (`{% if %} ... {% else %} ... {% endif %}`)
  - For loops (`{% for item in items %} ... {% endfor %}`)
  - Rich error messages with line/column information and "did you mean?" suggestions
- **code_generator**: Command-line tool that reads Wayland XML and generates C++ bindings from templates

The code generator reads Wayland protocol XML files (typically `/usr/share/wayland/wayland.xml`) and produces type-safe C++ wrappers with reactive event handling.

### Wayland Client Library

Located in `src/wayland/`, this provides generated Wayland protocol bindings:

- Generated from official Wayland protocol XML using the code generator
- Designed to integrate reactive programming with Wayland's event model
- Each protocol object exposes an `events()` method returning an `Observable<EventTypes...>`
- Event queuing strategy to handle the race between object construction and event subscription

Current architecture uses per-object event queues that buffer incoming events until the first subscription, ensuring no events are missed while maintaining simple, synchronous object semantics.

### Renderer

Located in `src/renderer/`, this provides basic graphics primitives:

- **Rasterizer**: Software rasterization for lines, shapes, and patterns
- Supports line thickness, filled rectangles/circles, and anti-aliasing
- PPM image output for testing and visualization

## Building

The project uses CMake with C++23 features:

```bash
cmake -B build -S .
cmake --build build
```

Run tests:

```bash
cd build && ctest
```

## Dependencies

- C++23 compiler (GCC 13+ or Clang 17+)
- CMake 3.24+
- Wayland development files (for protocol XML)

## Design Philosophy

This project explores several architectural ideas:

1. **Coroutines as first-class async primitives**: The async runtime avoids callback-heavy designs in favor of sequential-looking coroutine code with explicit control flow.

2. **Type-erased task interfaces**: The `BasicTask` implementation uses traits to customize behavior while maintaining type erasure for dynamic dispatch where needed.

3. **Reactive Wayland integration**: Rather than traditional callback-based Wayland clients, this library exposes protocol events as observable streams, enabling reactive composition patterns.

4. **Code generation for type safety**: Instead of hand-writing protocol bindings, the code generator ensures protocol changes automatically propagate through the codebase with compile-time safety.

## Current Limitations

- Wayland implementation is incomplete; only protocol binding generation is implemented
- Connection management and wire protocol handling not yet implemented
- Observable/AsyncQueue infrastructure referenced in generated code is not yet implemented
- No actual audio streaming functionality (despite the repository name)
- Limited platform support (Linux/Wayland only by design)

## Future Work

- Implement `AsyncQueue<T>` for event buffering with backpressure
- Complete Wayland connection handling and wire protocol implementation
- Add coroutine-aware observable primitives for reactive event handling
- Extend code generator to support additional protocol features (enums, bitfields)
- Add identifier sanitization to prevent keyword collisions in generated code

## License

MIT License - see individual file headers for copyright information.

## Author

Maikel Nadolski
