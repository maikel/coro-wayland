// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#pragma once

#include <compare>
#include <cstdint>

namespace ms {

struct FileDescriptor {
public:
  FileDescriptor() noexcept;
  explicit FileDescriptor(int handle);

  FileDescriptor(const FileDescriptor&) = delete;
  auto operator=(const FileDescriptor&) -> FileDescriptor& = delete;

  FileDescriptor(FileDescriptor&& other) noexcept;
  auto operator=(FileDescriptor&& other) noexcept -> FileDescriptor&;

  ~FileDescriptor();

  auto native_handle() const noexcept -> int;

  auto reset(int newHandle = -1) noexcept -> void;

  friend auto operator<=>(FileDescriptor, FileDescriptor) = default;

private:
  int mNativeHandle;
};

struct FileDescriptorHandle {
public:
  FileDescriptorHandle() noexcept : mNativeHandle(-1) {}

  FileDescriptorHandle(const FileDescriptor& fd) noexcept;

  explicit FileDescriptorHandle(int handle);

  auto native_handle() const noexcept -> int;

  friend auto operator<=>(FileDescriptorHandle, FileDescriptorHandle) = default;

private:
  int mNativeHandle;
};

} // namespace ms