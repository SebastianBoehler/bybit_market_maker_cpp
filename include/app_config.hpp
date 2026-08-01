#pragma once

#include <string>

struct AppConfig
{
    std::string symbol{"SUIUSDT"};
    std::string api_key;
    std::string api_secret;
    std::string base_url{"https://api.bybit.com"};
    std::string public_ws_url{"wss://stream.bybit.com/v5/public/linear"};
    std::string private_ws_url{"wss://stream.bybit.com/v5/private"};
    std::string side_mode{"both"};
    bool run_live{false};
    double budget_usd{10.0};
    double min_spread_bps{0.2};
    double spread_factor{1.0};
    double max_net_qty{100.0};
    double tp_safety_bps{0.5};
    int ladder_levels{3};
    double stop_loss_bps{-1.0};
    double gross_notional_cap{-1.0};
};

void load_env_file(const std::string &path);
AppConfig load_config(int argc, char **argv, const std::string &default_side_mode);
void validate_config(const AppConfig &config);
