#include "strategy.hpp"

#include <cmath>
#include <iostream>
#include <sstream>
#include <chrono>

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

void ExampleMarketMakerStrategy::on_snapshot(const MarketDataSnapshot &snapshot, TradingHelper &helper, bool live_trading, const PositionView &pos)
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
        // Bybit orderbook: "b" bids, "a" asks. Each entry: [price, size].
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

        // Spread: base on live spread but enforce a floor in bps.
        const double live_spread_bps = (live_spread / mid) * 1e4;
        const double target_spread_bps = std::max(min_spread_bps_, live_spread_bps * spread_factor_);
        const double half_spread_abs = (target_spread_bps * 0.0001) * mid;

        // Price rounding to tick size.
        const double raw_bid_px = mid - half_spread_abs;
        const double raw_ask_px = mid + half_spread_abs;
        const double bid_px = round_down(raw_bid_px, meta_.tick_size);
        const double ask_px = round_down(raw_ask_px, meta_.tick_size);

        // Base size: min tradable size.
        double base_qty = round_down(meta_.min_qty, meta_.lot_size);
        if (base_qty < meta_.min_qty)
            base_qty = meta_.min_qty;

        // Inventory skew: if net long, reduce/suspend new bids; if net short, reduce/suspend asks.
        double net_qty = pos.long_size - pos.short_size;
        double bid_scale = 1.0;
        double ask_scale = 1.0;
        if (std::abs(net_qty) > max_net_qty_)
        {
            if (net_qty > 0)
                bid_scale = 0.0; // too long; stop bidding
            else
                ask_scale = 0.0; // too short; stop offering
        }
        else
        {
            double skew_factor = 1.0 - (std::abs(net_qty) / max_net_qty_);
            if (net_qty > 0)
                bid_scale = std::max(0.2, skew_factor);
            else if (net_qty < 0)
                ask_scale = std::max(0.2, skew_factor);
        }

        std::cout << "[MM] " << snapshot.symbol << " mid=" << mid << " live_spread_bps=" << live_spread_bps
                  << " target_spread_bps=" << target_spread_bps << " bid@" << bid_px << " ask@" << ask_px
                  << " base_qty=" << base_qty << " net=" << net_qty << (live_trading ? " [live]" : " [dry-run]") << "\n";

        auto make_link = [&](const std::string &side)
        {
            const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::system_clock::now().time_since_epoch())
                                    .count();
            return side + "_mm_" + std::to_string(now_ms) + "_" + std::to_string(++order_counter_);
        };

        if (!live_trading || !helper.has_credentials())
        {
            return;
        }

        // Cancel previous working orders before placing fresh quotes to avoid stacking margin.
        helper.cancel_all(symbol_);

        // Gross notional guard: if both sides consume too much margin, skip making markets but still allow TP/SL.
        double gross_notional = (pos.long_size + pos.short_size) * mid;
        bool skip_new_quotes = (gross_notional_cap_ > 0.0 && gross_notional >= gross_notional_cap_);
        if (skip_new_quotes)
        {
            std::cout << "[MM] gross cap hit, skip new quotes gross=" << gross_notional << " cap=" << gross_notional_cap_ << "\n";
        }

        // Collect all orders for batch submission
        std::vector<std::vector<std::pair<std::string, std::string>>> batch_orders;

        // Place laddered quotes per side.
        if (!skip_new_quotes)
        {
            for (int level = 1; level <= ladder_levels_; ++level)
            {
                double level_offset = half_spread_abs * level;
                double bid_ladder_px = round_down(mid - level_offset, meta_.tick_size);
                double ask_ladder_px = round_down(mid + level_offset, meta_.tick_size);
                double lvl_qty = base_qty;
                if (bid_scale > 0.0)
                {
                    batch_orders.push_back({{"symbol", symbol_},
                                            {"side", "Buy"},
                                            {"orderType", "Limit"},
                                            {"qty", to_string_prec(lvl_qty * bid_scale)},
                                            {"price", to_string_prec(bid_ladder_px)},
                                            {"positionIdx", std::to_string(buy_pos_idx_)},
                                            {"orderLinkId", make_link("bid")},
                                            {"timeInForce", "GTC"}});
                }
                if (ask_scale > 0.0)
                {
                    batch_orders.push_back({{"symbol", symbol_},
                                            {"side", "Sell"},
                                            {"orderType", "Limit"},
                                            {"qty", to_string_prec(lvl_qty * ask_scale)},
                                            {"price", to_string_prec(ask_ladder_px)},
                                            {"positionIdx", std::to_string(sell_pos_idx_)},
                                            {"orderLinkId", make_link("ask")},
                                            {"timeInForce", "GTC"}});
                }
            }
        }

        // Take-profit: if net long, place a small ask at tp_spread_bps above mid; if net short, place a small bid below mid.
        if (net_qty > meta_.min_qty && ask_scale > 0.0)
        {
            double tp_px = round_down(mid + (tp_spread_bps_ * 0.0001) * mid, meta_.tick_size);
            batch_orders.push_back({{"symbol", symbol_},
                                    {"side", "Sell"},
                                    {"orderType", "Limit"},
                                    {"qty", to_string_prec(base_qty)},
                                    {"price", to_string_prec(tp_px)},
                                    {"positionIdx", std::to_string(sell_pos_idx_)},
                                    {"orderLinkId", make_link("tp_sell")},
                                    {"timeInForce", "GTC"}});
        }
        else if (net_qty < -meta_.min_qty && bid_scale > 0.0)
        {
            double tp_px = round_down(mid - (tp_spread_bps_ * 0.0001) * mid, meta_.tick_size);
            batch_orders.push_back({{"symbol", symbol_},
                                    {"side", "Buy"},
                                    {"orderType", "Limit"},
                                    {"qty", to_string_prec(base_qty)},
                                    {"price", to_string_prec(tp_px)},
                                    {"positionIdx", std::to_string(buy_pos_idx_)},
                                    {"orderLinkId", make_link("tp_buy")},
                                    {"timeInForce", "GTC"}});
        }

        // Submit all orders in one batch request
        if (!batch_orders.empty())
        {
            helper.batch_submit_orders(batch_orders);
        }

        // Stop-loss: flatten if price moves past threshold from entry.
        if (stop_loss_bps_ > 0.0)
        {
            double stop_mult = stop_loss_bps_ * 0.0001;
            if (pos.long_size > meta_.min_qty && pos.long_entry > 0.0)
            {
                double stop_px = pos.long_entry * (1.0 - stop_mult);
                std::cout << "[SLDBG] long mid=" << mid << " entry=" << pos.long_entry << " stop=" << stop_px << " size=" << pos.long_size << "\n";
                if (mid <= stop_px)
                {
                    helper.submit_market_order(symbol_, "Sell", to_string_prec(pos.long_size), sell_pos_idx_, make_link("sl_long"));
                    std::cout << "[SL] flattening long size=" << pos.long_size << " at mid=" << mid << " stop=" << stop_px << "\n";
                }
            }
            if (pos.short_size > meta_.min_qty && pos.short_entry > 0.0)
            {
                double stop_px = pos.short_entry * (1.0 + stop_mult);
                std::cout << "[SLDBG] short mid=" << mid << " entry=" << pos.short_entry << " stop=" << stop_px << " size=" << pos.short_size << "\n";
                if (mid >= stop_px)
                {
                    helper.submit_market_order(symbol_, "Buy", to_string_prec(pos.short_size), buy_pos_idx_, make_link("sl_short"));
                    std::cout << "[SL] flattening short size=" << pos.short_size << " at mid=" << mid << " stop=" << stop_px << "\n";
                }
            }
        }
    }
    catch (const std::exception &ex)
    {
        std::cerr << "Error processing snapshot for " << snapshot.symbol << ": " << ex.what() << "\n";
    }
}
