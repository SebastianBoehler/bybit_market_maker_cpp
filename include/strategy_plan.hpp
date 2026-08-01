#pragma once

#include <string>
#include <vector>

#include "strategy_types.hpp"

enum class StrategyMode
{
    Both,
    LongOnly
};

struct StrategySettings
{
    std::string symbol;
    InstrumentMeta instrument;
    StrategyMode mode{StrategyMode::Both};
    double budget_usd{0.0};
    double min_spread_bps{0.0};
    double spread_factor{1.0};
    int long_position_idx{1};
    int short_position_idx{2};
    double max_net_qty{0.0};
    double tp_safety_bps{0.0};
    int ladder_levels{1};
    double stop_loss_bps{-1.0};
    double gross_notional_cap{-1.0};
    FeeRates fees;
};

struct MarketTop
{
    double bid{0.0};
    double ask{0.0};
    double risk_price{0.0};
};

struct PlannedOrder
{
    std::string name;
    std::string side;
    std::string order_type{"Limit"};
    double qty{0.0};
    double price{0.0};
    int position_idx{0};
    bool reduce_only{false};
    std::string time_in_force{"PostOnly"};
};

struct StrategyPlan
{
    std::vector<PlannedOrder> limit_orders;
    std::vector<PlannedOrder> market_orders;
    bool stop_triggered{false};
};

StrategyPlan plan_orders(const StrategySettings &settings,
                         MarketTop market,
                         const PositionView &position,
                         const std::vector<RestingOrder> &resting_orders,
                         bool force_exit = false);
