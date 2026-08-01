#pragma once

#include <string>
#include <vector>

#include "strategy_runtime.hpp"

class IStrategy
{
public:
    virtual ~IStrategy() = default;
    virtual void on_snapshot(const MarketDataSnapshot &snapshot,
                             TradingHelper &helper,
                             bool live_trading,
                             const PositionView &position) = 0;
    virtual void sync_open_orders(const OpenOrderSnapshot &snapshot) = 0;
    virtual void cancel_working(TradingHelper &helper) = 0;
    virtual const std::string &owned_order_prefix() const = 0;
};

class ExampleMarketMakerStrategy : public IStrategy
{
public:
    ExampleMarketMakerStrategy(std::string symbol,
                               InstrumentMeta meta,
                               double budget_usd,
                               double min_spread_bps = 1.0,
                               double spread_factor = 1.0,
                               int long_position_idx = 1,
                               int short_position_idx = 2,
                               double max_net_qty = 50.0,
                               double tp_safety_bps = 0.5,
                               int ladder_levels = 3,
                               double stop_loss_bps = -1.0,
                               double gross_notional_cap = -1.0,
                               FeeRates fees = {},
                               std::vector<RestingOrder> resting_orders = {});

    void on_snapshot(const MarketDataSnapshot &snapshot,
                     TradingHelper &helper,
                     bool live_trading,
                     const PositionView &position) override;
    void sync_open_orders(const OpenOrderSnapshot &snapshot) override { runtime_.sync_open_orders(snapshot); }
    void cancel_working(TradingHelper &helper) override { runtime_.cancel_working(helper); }
    const std::string &owned_order_prefix() const override { return runtime_.owned_order_prefix(); }

private:
    StrategySettings settings_;
    StrategyRuntime runtime_;
};

class LongOnlyMarketMakerStrategy : public IStrategy
{
public:
    LongOnlyMarketMakerStrategy(std::string symbol,
                                InstrumentMeta meta,
                                double budget_usd,
                                double min_spread_bps = 1.0,
                                double spread_factor = 1.0,
                                int long_position_idx = 1,
                                int short_position_idx = 2,
                                double max_net_qty = 50.0,
                                double tp_safety_bps = 0.5,
                                int ladder_levels = 3,
                                double stop_loss_bps = -1.0,
                                double gross_notional_cap = -1.0,
                                FeeRates fees = {},
                                std::vector<RestingOrder> resting_orders = {});

    void on_snapshot(const MarketDataSnapshot &snapshot,
                     TradingHelper &helper,
                     bool live_trading,
                     const PositionView &position) override;
    void sync_open_orders(const OpenOrderSnapshot &snapshot) override { runtime_.sync_open_orders(snapshot); }
    void cancel_working(TradingHelper &helper) override { runtime_.cancel_working(helper); }
    const std::string &owned_order_prefix() const override { return runtime_.owned_order_prefix(); }

private:
    StrategySettings settings_;
    StrategyRuntime runtime_;
};
