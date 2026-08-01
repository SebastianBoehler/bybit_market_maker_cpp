#pragma once

#include <cstddef>
#include <initializer_list>
#include <string>

#include <nlohmann/json.hpp>

nlohmann::json validate_bybit_response(const std::string &body,
                                       std::size_t expected_batch_legs = 0,
                                       std::initializer_list<long long> allowed_codes = {});
