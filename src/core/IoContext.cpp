// SPDX-License-Identifier: MIT

#include "IoContext.hpp"

#include <sys/poll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <system_error>

namespace ms
{
IoContext::IoContext()
{
    mWakeupHandle = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (mWakeupHandle == -1)
    {
        throw std::system_error(errno, std::generic_category(), "Failed to create eventfd");
    }
}

IoContext::~IoContext()
{
    ::close(mWakeupHandle);
}

void IoContext::enqueue(IoContextTask& task)
{
    {
        std::lock_guard lock(mTasksMutex);
        mTasks.push_back(&task);
    }
    uint64_t value = 1;
    while (::write(mWakeupHandle, &value, sizeof(value)) == -1) {
        if (errno != EINTR) {
            throw std::system_error(errno, std::generic_category(), "Failed to write to eventfd");
        }
    }
}

void IoContext::run() noexcept
try {
    std::vector<IoContextTask*> tasksToProcess;
    while (true)
    {
        {
            std::lock_guard lock(mTasksMutex);
            if (mStopRequested && mTasks.empty())
            {
                break;
            }
            tasksToProcess.swap(mTasks);
        }

        for (IoContextTask* task : tasksToProcess)
        {
            task->doCompletion(task);
        }
        tasksToProcess.clear();

        ::pollfd fds[] = {
            {mWakeupHandle, POLLIN, 0},
        };
        if (::poll(fds, 1, -1) == -1) {
            if (errno == EINTR) {
                continue;
            } else {
                throw std::system_error(errno, std::generic_category(), "poll() failed");
            }
        }

        if (fds[0].revents & POLLIN)
        {
            uint64_t value;
            ::read(mWakeupHandle, &value, sizeof(value));
        }
    }
} catch (...) {
    // Explicit terminate for static analysis tools that warn about noexcept violations
    std::terminate();
}

void IoContext::request_stop()
{
    {
        std::lock_guard lock(mTasksMutex);
        mStopRequested = true;
    }
    uint64_t value = 1;
    while (::write(mWakeupHandle, &value, sizeof(value)) == -1) {
        if (errno != EINTR) {
            throw std::system_error(errno, std::generic_category(), "Failed to write to eventfd");
        }
    }
}
} // namespace ms