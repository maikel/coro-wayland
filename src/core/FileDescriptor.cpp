// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Maikel Nadolski <maikel.nadolski@gmail.com>

#include "FileDescriptor.hpp"

#include <unistd.h>

namespace cw {

FileDescriptor::FileDescriptor() noexcept : mNativeHandle(-1) {}

FileDescriptor::FileDescriptor(int handle) : mNativeHandle(handle) {}

FileDescriptor::FileDescriptor(FileDescriptor&& other) noexcept
    : mNativeHandle(other.mNativeHandle) {
  other.mNativeHandle = -1;
}

auto FileDescriptor::operator=(FileDescriptor&& other) noexcept -> FileDescriptor& {
  if (this != &other) {
    this->reset(other.mNativeHandle);
    other.mNativeHandle = -1;
  }
  return *this;
}

FileDescriptor::~FileDescriptor() { this->reset(); }

auto FileDescriptor::native_handle() const noexcept -> int { return mNativeHandle; }

auto FileDescriptor::reset(int newHandle) noexcept -> void {
  if (mNativeHandle != -1) {
    ::close(mNativeHandle);
  }
  mNativeHandle = newHandle;
}

FileDescriptorHandle::FileDescriptorHandle(int handle) : mNativeHandle(handle) {}

FileDescriptorHandle::FileDescriptorHandle(const FileDescriptor& fd) noexcept
    : mNativeHandle(fd.native_handle()) {}

auto FileDescriptorHandle::native_handle() const noexcept -> int { return mNativeHandle; }

} // namespace cw