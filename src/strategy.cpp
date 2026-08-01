#include "strategy.hpp"

ExampleMarketMakerStrategy::ExampleMarketMakerStrategy(std::string symbol,
                                                       InstrumentMeta meta,
                                                       double budget_usd,
                                                       double min_spread_bps,
                                                       double spread_factor,
                                                       int long_position_idx,
                                                       int short_position_idx,
                                                       double max_net_qty,
                                                       double tp_safety_bps,
                                                       int ladder_levels,
                                                       double stop_loss_bps,
                                                       double gross_notional_cap,
                                                       FeeRates fees,
                                                       std::vector<RestingOrder> resting_orders)
    : settings_{symbol, meta, StrategyMode::Both, budget_usd, min_spread_bps, spread_factor,
                long_position_idx, short_position_idx, max_net_qty, tp_safety_bps,
                ladder_levels, stop_loss_bps, gross_notional_cap, fees},
      runtime_(std::move(symbol), std::move(resting_orders)) {}

void ExampleMarketMakerStrategy::on_snapshot(const MarketDataSnapshot &snapshot,
                                             TradingHelper &helper,
                                             bool live_trading,
                                             const PositionView &position)
{
    runtime_.on_snapshot(settings_, snapshot, helper, live_trading, position);
}
