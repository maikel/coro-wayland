#include "Task.hpp"

auto test_task() -> ms::Task<int> {
    co_return 42;
}

int main() {
    ms::Task<int> task = test_task();
}