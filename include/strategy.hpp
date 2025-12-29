#pragma once

#include <string>

#include "trading_helper.hpp"

struct InstrumentMeta
{
    double tick_size{0.0};
    double lot_size{0.0};
    double min_qty{0.0};
};

// Strategy interface: consume market data snapshots and optionally issue orders via TradingHelper.
class IStrategy
{
public:
    virtual ~IStrategy() = default;
    virtual void on_snapshot(const MarketDataSnapshot &snapshot, TradingHelper &helper, bool live_trading) = 0;
};

// Market-making strategy: sizes from USD budget, respects tick/lot/min, and bases spread on live spread.
class ExampleMarketMakerStrategy : public IStrategy
{
public:
    ExampleMarketMakerStrategy(std::string symbol,
                               InstrumentMeta meta,
                               double budget_usd,
                               double min_spread_bps = 1.0,
                               double spread_factor = 1.0,
                               int buy_pos_idx = 1,
                               int sell_pos_idx = 2)
        : symbol_(std::move(symbol)),
          meta_(meta),
          budget_usd_(budget_usd),
          min_spread_bps_(min_spread_bps),
          spread_factor_(spread_factor),
          buy_pos_idx_(buy_pos_idx),
          sell_pos_idx_(sell_pos_idx) {}

    void on_snapshot(const MarketDataSnapshot &snapshot, TradingHelper &helper, bool live_trading) override;

private:
    std::string symbol_;
    InstrumentMeta meta_;
    double budget_usd_;
    double min_spread_bps_;
    double spread_factor_;
    uint64_t order_counter_{0};
    int buy_pos_idx_{0};
    int sell_pos_idx_{0};
};
