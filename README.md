# coro-wayland

A modern C++23 coroutine-based async runtime with reactive Wayland client library. This project demonstrates advanced coroutine patterns, structured concurrency, and reactive programming for building efficient Wayland applications.

## ⚠️ Experimental Status

**This project is highly experimental and in active development.** The APIs are unstable and will change. It's not ready for production use or external contributions yet. I'm using this as a research platform to explore different async patterns in C++.

## What is coro-wayland?

Traditional Wayland clients rely heavily on callbacks and manual state management, making complex applications difficult to reason about. coro-wayland takes a different approach:

- **Coroutine-first**: Write sequential async code without callback hell
- **Reactive events**: Wayland events as composable Observable streams
- **Type-safe protocols**: Generated bindings ensure compile-time correctness
- **Structured concurrency**: AsyncScope with automatic cancellation propagation
- **Smart resource management**: `use_resource()` for async RAII patterns

## Features

### Core Async Runtime

A complete coroutine-based async runtime (`src/core/`) with structured concurrency:

- **Task\<T\>** / **IoTask\<T\>**: Lazily-evaluated coroutine tasks with cancellation support
- **IoContext**: Single-threaded event loop with io_uring-style architecture
- **AsyncScope**: Structured task spawning with automatic lifetime management
- **AsyncQueue\<T\>**: Async producer/consumer queues with backpressure
- **AsyncUnorderedMap\<K,V\>**: Reactive key-value storage with observable updates
- **Observable\<T\>**: Type-erased reactive streams for event composition
- **use_resource()**: Async RAII - resources cleaned up after last coroutine reference
- **when_all** / **when_any**: Concurrent composition primitives
- **sync_wait**: Bridge synchronous and asynchronous worlds

### Wayland Protocol Bindings

Type-safe, reactive Wayland client library (`src/wayland/`):

- **Client**: High-level Wayland connection management
- **Connection**: Low-level wire protocol handling
- **FrameBufferPool**: Double-buffered shared memory management
- **Generated bindings**: Type-safe protocol objects from XML definitions
- **Reactive events**: Each Wayland object exposes `events()` returning `Observable<...>`
- **Automatic cleanup**: Resources tied to coroutine lifetimes via `use_resource()`

### Code Generation

Custom tooling for generating C++ from Wayland protocol XML (`src/code_generator/`):

- **WaylandXmlParser**: Parses official Wayland protocol XML files
- **JinjaTemplateEngine**: Template engine with variable substitution, conditionals, loops
- **code_generator**: CLI tool generating type-safe C++ bindings from templates
- **Rich error messages**: Line/column information with "did you mean?" suggestions

## Building

### Requirements

- **Compiler**: GCC 13+, Clang 17+, or newer (C++23 support required)
- **CMake**: 3.24 or newer
- **Wayland**: XML files
- **System**: Linux (Wayland is Linux-specific)

### Build Instructions

```bash
# Configure
cmake -B build -S .

# Build
cmake --build build

# Run tests
cd build && ctest

# Build with optimizations
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Project Structure

```
coro-wayland/
├── src/
│   ├── core/              # Async runtime (Task, IoContext, Observable, etc.)
│   │   ├── include/       # Public headers
│   │   └── tests/         # Unit tests
│   ├── wayland/           # Wayland client library
│   │   ├── Client.cpp     # High-level Wayland client
│   │   ├── Connection.cpp # Wire protocol implementation
│   │   ├── FrameBufferPool.cpp  # Shared memory management
│   │   └── include/       # Public Wayland API
│   ├── code_generator/    # Protocol binding generator
│   │   ├── code_generator.cpp
│   │   ├── WaylandXmlParser.cpp
│   │   └── JinjaTemplateEngine.cpp
│   ├── logging/           # Logging utilities
│   └── renderer/          # Basic software rasterizer
├── docs/                  # Documentation
├── CMakeLists.txt
└── README.md
```

## Design Philosophy

### Coroutines Over Callbacks

Traditional Wayland clients use callbacks for event handling, leading to fragmented control flow. coro-wayland uses C++20 coroutines to write sequential, easy-to-understand async code.

### Structured Concurrency

The `AsyncScope` ensures spawned tasks are automatically cancelled and cleaned up when the scope exits, preventing resource leaks and dangling tasks.

### Reactive Event Streams

Wayland protocol events are exposed as `Observable<T>` streams, enabling functional composition patterns instead of managing callbacks manually.

### Async RAII with use_resource()

Resources are tied to coroutine lifetimes, ensuring cleanup happens asynchronously after the last reference goes out of scope.

## Current Status

**Working:**
- Core async runtime with full feature set
- Wayland wire protocol implementation
- Client connection management
- Frame buffer pool with double buffering
- Code generator for protocol bindings
- Working demo applications
- Comprehensive unit tests

**In Progress:**
- Additional XDG shell protocols
- Input device handling (keyboard, mouse, touch)
- Vulkan/OpenGL renderer integration
- Documentation and examples

## Contributing

This is a personal research project exploring modern C++ async patterns. I'm not accepting contributions at this time as the project is still too experimental and the APIs are changing frequently. Feel free to watch or fork the project if you're interested in the ideas being explored here.

## License

MIT License - see individual source files for copyright information.

## Author

Maikel Nadolski <maikel.nadolski@gmail.com>

## License

MIT License - see individual file headers for copyright information.

## Author

Maikel Nadolski
