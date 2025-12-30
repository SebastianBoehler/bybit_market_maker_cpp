#include "strategy.hpp"

#include <cmath>
#include <iostream>
#include <sstream>
#include <chrono>
#include <nlohmann/json.hpp>

namespace
{
    double round_down(double value, double step)
    {
        if (step <= 0)
            return value;
        return std::floor(value / step) * step;
    }

    std::string to_string_prec(double v)
    {
        std::ostringstream oss;
        oss.setf(std::ios::fixed);
        oss.precision(8);
        oss << v;
        return oss.str();
    }
} // namespace

void LongOnlyMarketMakerStrategy::on_snapshot(const MarketDataSnapshot &snapshot, TradingHelper &helper, bool live_trading, const PositionView &pos)
{
    const nlohmann::json *ob_ptr = &snapshot.orderbook;
    if (snapshot.orderbook.contains("result"))
        ob_ptr = &snapshot.orderbook["result"];
    const auto &ob = *ob_ptr;
    if (!ob.contains("b") || !ob.contains("a"))
    {
        std::cerr << "Orderbook missing b/a for " << snapshot.symbol << "\n";
        return;
    }
    try
    {
        const auto &bids = ob["b"];
        const auto &asks = ob["a"];
        if (bids.empty() || asks.empty())
        {
            std::cerr << "Orderbook empty for " << snapshot.symbol << "\n";
            return;
        }
        const double best_bid = std::stod(bids[0][0].get<std::string>());
        const double best_ask = std::stod(asks[0][0].get<std::string>());
        const double live_spread = best_ask - best_bid;
        if (live_spread <= 0)
        {
            std::cerr << "Non-positive spread for " << snapshot.symbol << "\n";
            return;
        }
        const double mid = 0.5 * (best_ask + best_bid);

        // Spread floor.
        const double live_spread_bps = (live_spread / mid) * 1e4;
        const double target_spread_bps = std::max(min_spread_bps_, live_spread_bps * spread_factor_);
        const double half_spread_abs = (target_spread_bps * 0.0001) * mid;

        // Price rounding.
        const double raw_bid_px = mid - half_spread_abs;
        const double bid_px = round_down(raw_bid_px, meta_.tick_size);

        // Base size: min tradable size.
        double base_qty = round_down(meta_.min_qty, meta_.lot_size);
        if (base_qty < meta_.min_qty)
            base_qty = meta_.min_qty;

        // Inventory control: respect max net qty; if too long, pause bids.
        double net_qty = pos.long_size - pos.short_size;
        double bid_scale = 1.0;
        if (std::abs(net_qty) > max_net_qty_)
            bid_scale = 0.0;
        else
            bid_scale = std::max(0.2, 1.0 - (net_qty / max_net_qty_));

        std::cout << "[MM-LO] " << snapshot.symbol << " mid=" << mid << " live_spread_bps=" << live_spread_bps
                  << " target_spread_bps=" << target_spread_bps << " bid@" << bid_px
                  << " base_qty=" << base_qty << " net=" << net_qty << (live_trading ? " [live]" : " [dry-run]") << "\n";

        auto make_link = [&](const std::string &side)
        {
            const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::system_clock::now().time_since_epoch())
                                    .count();
            return side + "_mmlo_" + std::to_string(now_ms) + "_" + std::to_string(++order_counter_);
        };

        if (!live_trading || !helper.has_credentials())
            return;

        // Cancel previous working orders.
        helper.cancel_all(symbol_);

        // Gross notional guard.
        double gross_notional = (pos.long_size + pos.short_size) * mid;
        bool skip_new_bids = (gross_notional_cap_ > 0.0 && gross_notional >= gross_notional_cap_);
        if (skip_new_bids)
        {
            std::cout << "[MM-LO] gross cap hit, skip new bids gross=" << gross_notional << " cap=" << gross_notional_cap_ << "\n";
        }

        // Place bid ladder only.
        if (!skip_new_bids && bid_scale > 0.0)
        {
            for (int level = 1; level <= ladder_levels_; ++level)
            {
                double level_offset = half_spread_abs * level;
                double bid_ladder_px = round_down(mid - level_offset, meta_.tick_size);
                double lvl_qty = base_qty;
                helper.submit_limit_order(symbol_, "Buy", to_string_prec(lvl_qty * bid_scale), to_string_prec(bid_ladder_px), buy_pos_idx_, "Limit", make_link("bid"));
            }
        }

        // Take-profit sells to lighten inventory.
        if (net_qty > meta_.min_qty)
        {
            double tp_px = round_down(mid + (tp_spread_bps_ * 0.0001) * mid, meta_.tick_size);
            helper.submit_limit_order(symbol_, "Sell", to_string_prec(base_qty), to_string_prec(tp_px), sell_pos_idx_, "Limit", make_link("tp_sell"));
        }

        // Stop-loss: flatten long if price falls beyond threshold.
        if (stop_loss_bps_ > 0.0 && pos.long_size > meta_.min_qty && pos.long_entry > 0.0)
        {
            double stop_px = pos.long_entry * (1.0 - stop_loss_bps_ * 0.0001);
            if (mid <= stop_px)
            {
                helper.submit_market_order(symbol_, "Sell", to_string_prec(pos.long_size), sell_pos_idx_, make_link("sl_long"));
                std::cout << "[SL-LO] flattening long size=" << pos.long_size << " at mid=" << mid << " stop=" << stop_px << "\n";
            }
        }
    }
    catch (const std::exception &ex)
    {
        std::cerr << "Error processing snapshot for " << snapshot.symbol << ": " << ex.what() << "\n";
    }
}
