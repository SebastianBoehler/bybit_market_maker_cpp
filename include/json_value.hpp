#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

double json_finite_number(const nlohmann::json &value, const std::string &context);
double json_finite_number_or_zero(const nlohmann::json &value, const std::string &context);
int json_integer(const nlohmann::json &value, const std::string &context);
std::uint64_t json_unsigned(const nlohmann::json &value, const std::string &context);
bool json_boolean(const nlohmann::json &value, const std::string &context);
