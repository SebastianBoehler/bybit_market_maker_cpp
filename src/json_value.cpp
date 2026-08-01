#include "json_value.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace
{
template <typename Parse>
auto parse_complete(const std::string &text, const std::string &context, Parse parse)
{
    if (text.empty())
        throw std::runtime_error(context + " is empty");
    std::size_t consumed = 0;
    const auto parsed = parse(text, &consumed);
    if (consumed != text.size())
        throw std::runtime_error(context + " has trailing characters");
    return parsed;
}
} // namespace

double json_finite_number(const nlohmann::json &value, const std::string &context)
{
    double parsed = 0.0;
    try
    {
        if (value.is_number())
            parsed = value.get<double>();
        else if (value.is_string())
            parsed = parse_complete(value.get<std::string>(), context,
                                    [](const std::string &text, std::size_t *consumed)
                                    { return std::stod(text, consumed); });
        else
            throw std::runtime_error(context + " is not numeric");
    }
    catch (const std::runtime_error &)
    {
        throw;
    }
    catch (const std::exception &error)
    {
        throw std::runtime_error(context + " is malformed: " + error.what());
    }
    if (!std::isfinite(parsed))
        throw std::runtime_error(context + " is not finite");
    return parsed;
}

double json_finite_number_or_zero(const nlohmann::json &value, const std::string &context)
{
    if (value.is_string() && value.get_ref<const std::string &>().empty())
        return 0.0;
    return json_finite_number(value, context);
}

int json_integer(const nlohmann::json &value, const std::string &context)
{
    long long parsed = 0;
    if (value.is_number_integer())
        parsed = value.get<long long>();
    else if (value.is_string())
        parsed = parse_complete(value.get<std::string>(), context,
                                [](const std::string &text, std::size_t *consumed)
                                { return std::stoll(text, consumed); });
    else
        throw std::runtime_error(context + " is not an integer");
    if (parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max())
        throw std::runtime_error(context + " is outside integer range");
    return static_cast<int>(parsed);
}

std::uint64_t json_unsigned(const nlohmann::json &value, const std::string &context)
{
    if (value.is_number_unsigned())
        return value.get<std::uint64_t>();
    if (value.is_number_integer())
    {
        const auto parsed = value.get<long long>();
        if (parsed < 0)
            throw std::runtime_error(context + " is negative");
        return static_cast<std::uint64_t>(parsed);
    }
    if (value.is_string())
    {
        const auto text = value.get<std::string>();
        if (!text.empty() && text.front() == '-')
            throw std::runtime_error(context + " is negative");
        return parse_complete(text, context,
                              [](const std::string &text, std::size_t *consumed)
                              { return std::stoull(text, consumed); });
    }
    throw std::runtime_error(context + " is not an unsigned integer");
}

bool json_boolean(const nlohmann::json &value, const std::string &context)
{
    if (value.is_boolean())
        return value.get<bool>();
    if (value.is_string())
    {
        const auto text = value.get<std::string>();
        if (text == "true")
            return true;
        if (text == "false")
            return false;
    }
    throw std::runtime_error(context + " is not a boolean");
}
