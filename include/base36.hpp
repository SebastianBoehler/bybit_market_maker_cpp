#pragma once

#include <cstdint>
#include <string>

inline std::string encode_base36(std::uint64_t value)
{
    static constexpr char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    std::string encoded;
    do
    {
        encoded.push_back(digits[value % 36]);
        value /= 36;
    } while (value != 0);
    return {encoded.rbegin(), encoded.rend()};
}
