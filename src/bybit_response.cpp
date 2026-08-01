#include "bybit_response.hpp"

#include "json_value.hpp"

#include <algorithm>
#include <stdexcept>

namespace
{
long long response_code(const nlohmann::json &value)
{
    return json_integer(value, "Bybit response code");
}
} // namespace

nlohmann::json validate_bybit_response(const std::string &body,
                                       std::size_t expected_batch_legs,
                                       std::initializer_list<long long> allowed_codes)
{
    nlohmann::json response;
    try
    {
        response = nlohmann::json::parse(body);
    }
    catch (const std::exception &error)
    {
        throw std::runtime_error(std::string{"invalid Bybit JSON response: "} + error.what());
    }
    if (!response.contains("retCode"))
        throw std::runtime_error("Bybit response missing retCode");
    const auto code = response_code(response["retCode"]);
    if (code != 0 && std::find(allowed_codes.begin(), allowed_codes.end(), code) == allowed_codes.end())
    {
        const std::string message = response.value("retMsg", "unknown error");
        throw std::runtime_error("Bybit retCode " + std::to_string(code) + ": " + message);
    }
    if (expected_batch_legs > 0)
    {
        if (!response.contains("result") || !response["result"].contains("list") ||
            !response["result"]["list"].is_array() ||
            response["result"]["list"].size() != expected_batch_legs)
        {
            throw std::runtime_error("Bybit batch result count mismatch");
        }
        if (!response.contains("retExtInfo") || !response["retExtInfo"].contains("list") ||
            !response["retExtInfo"]["list"].is_array() ||
            response["retExtInfo"]["list"].size() != expected_batch_legs)
        {
            throw std::runtime_error("Bybit batch status count mismatch");
        }
        for (std::size_t i = 0; i < expected_batch_legs; ++i)
        {
            const auto &leg = response["retExtInfo"]["list"][i];
            if (!leg.contains("code"))
                throw std::runtime_error("Bybit batch leg " + std::to_string(i) + " missing code");
            const auto leg_code = response_code(leg["code"]);
            if (leg_code != 0)
            {
                const std::string message = leg.value("msg", "unknown error");
                throw std::runtime_error("Bybit batch leg " + std::to_string(i) + " code " +
                                         std::to_string(leg_code) + ": " + message);
            }
        }
    }
    return response;
}
