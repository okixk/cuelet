#pragma once

#include <iostream>
#include <stdexcept>
#include <string>

namespace cuelet_linux::tests {

inline void require(bool condition, const char* expression, const char* file, int line)
{
    if (condition) {
        return;
    }

    throw std::runtime_error(
        std::string(file) + ":" + std::to_string(line) + ": requirement failed: " + expression);
}

template <typename Function>
int run(const char* suiteName, Function&& tests)
{
    try {
        tests();
        std::cout << suiteName << " passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << suiteName << " failed: " << error.what() << '\n';
        return 1;
    }
}

} // namespace cuelet_linux::tests

#define CUELET_REQUIRE(expression) \
    ::cuelet_linux::tests::require(static_cast<bool>(expression), #expression, __FILE__, __LINE__)
