#include "app_config.hpp"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <stdexcept>

namespace
{
std::string environment(const char *name, const std::string &fallback = {})
{
    const char *value = std::getenv(name);
    return value == nullptr ? fallback : std::string{value};
}

void trim(std::string &value)
{
    constexpr const char *whitespace = " \t\r\n";
    const auto first = value.find_first_not_of(whitespace);
    if (first == std::string::npos)
    {
        value.clear();
        return;
    }
    value = value.substr(first, value.find_last_not_of(whitespace) - first + 1);
}

double decimal_environment(const char *name, const char *fallback)
{
    const auto text = environment(name, fallback);
    std::size_t consumed = 0;
    const double value = std::stod(text, &consumed);
    if (consumed != text.size())
        throw std::runtime_error(std::string{name} + " must be a complete number");
    return value;
}

int integer_environment(const char *name, const char *fallback)
{
    const auto text = environment(name, fallback);
    std::size_t consumed = 0;
    const int value = std::stoi(text, &consumed);
    if (consumed != text.size())
        throw std::runtime_error(std::string{name} + " must be a complete integer");
    return value;
}
} // namespace

void load_env_file(const std::string &path)
{
    std::ifstream input(path);
    std::string line;
    while (std::getline(input, line))
    {
        trim(line);
        if (line.empty() || line.front() == '#')
            continue;
        const auto separator = line.find('=');
        if (separator == std::string::npos || separator == 0)
            continue;
        auto key = line.substr(0, separator);
        auto value = line.substr(separator + 1);
        trim(key);
        trim(value);
        if (key.empty() || std::getenv(key.c_str()) != nullptr)
            continue;
#ifdef _WIN32
        _putenv_s(key.c_str(), value.c_str());
#else
        setenv(key.c_str(), value.c_str(), 0);
#endif
    }
}

AppConfig load_config(int argc, char **argv, const std::string &default_side_mode)
{
    load_env_file(".env");
    AppConfig config;
    config.symbol = argc > 1 ? argv[1] : environment("BYBIT_SYMBOL", config.symbol);
    config.api_key = environment("BYBIT_API_KEY");
    config.api_secret = environment("BYBIT_API_SECRET");
    config.base_url = environment("BYBIT_BASE_URL", config.base_url);
    config.public_ws_url = environment("BYBIT_WS_PUBLIC_URL", config.public_ws_url);
    config.private_ws_url = environment("BYBIT_WS_PRIVATE_URL", config.private_ws_url);
    config.side_mode = environment("BYBIT_SIDE_MODE", default_side_mode);
    const auto run_live = environment("BYBIT_RUN_LIVE", "0");
    if (run_live != "0" && run_live != "1")
        throw std::runtime_error("BYBIT_RUN_LIVE must be 0 or 1");
    config.run_live = run_live == "1";
    config.budget_usd = decimal_environment("BYBIT_BUDGET_USD", "10.0");
    config.min_spread_bps = decimal_environment("BYBIT_MIN_SPREAD_BPS", "0.2");
    config.spread_factor = decimal_environment("BYBIT_SPREAD_FACTOR", "1.0");
    config.max_net_qty = decimal_environment("BYBIT_MAX_NET_QTY", "100.0");
    config.tp_safety_bps = decimal_environment("BYBIT_TP_SPREAD_BPS", "0.5");
    config.ladder_levels = integer_environment("BYBIT_LADDER_LEVELS", "3");
    config.stop_loss_bps = decimal_environment("BYBIT_STOP_LOSS_BPS", "-1");
    config.gross_notional_cap = decimal_environment("BYBIT_GROSS_NOTIONAL_CAP", "-1");
    validate_config(config);
    return config;
}

void validate_config(const AppConfig &config)
{
    if (config.symbol.empty())
        throw std::runtime_error("symbol must not be empty");
    if (config.base_url.empty())
        throw std::runtime_error("Bybit REST base URL must not be empty");
    if (config.public_ws_url.empty())
        throw std::runtime_error("Bybit public websocket URL must not be empty");
    if (config.side_mode != "both" && config.side_mode != "long_only")
        throw std::runtime_error("side mode must be both or long_only");
    const double numeric_values[] = {config.budget_usd, config.min_spread_bps,
                                     config.spread_factor, config.max_net_qty,
                                     config.tp_safety_bps, config.stop_loss_bps,
                                     config.gross_notional_cap};
    for (double value : numeric_values)
        if (!std::isfinite(value))
            throw std::runtime_error("strategy numeric settings must be finite");
    if (config.budget_usd <= 0.0 || config.max_net_qty <= 0.0 || config.ladder_levels <= 0 ||
        config.spread_factor <= 0.0 || config.min_spread_bps < 0.0 || config.tp_safety_bps < 0.0)
        throw std::runtime_error("strategy sizing and spread settings must be positive");
    if (config.stop_loss_bps >= 10000.0)
        throw std::runtime_error("enabled stop loss must be below 10000 bps");
    if (!config.run_live)
        return;
    if (config.private_ws_url.empty())
        throw std::runtime_error("Bybit private websocket URL must not be empty in live mode");
    if (config.ladder_levels > 9)
        throw std::runtime_error("live ladder levels must not exceed 9");
    if (config.api_key.empty() || config.api_secret.empty())
        throw std::runtime_error("live trading requires API key/secret");
    if (config.gross_notional_cap <= 0.0)
        throw std::runtime_error("live trading requires a positive gross notional cap");
}
