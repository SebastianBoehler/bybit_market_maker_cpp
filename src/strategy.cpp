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

void ExampleMarketMakerStrategy::on_snapshot(const MarketDataSnapshot &snapshot, TradingHelper &helper, bool live_trading)
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

        // Size from budget, rounded to lot size, respecting min qty.
        double qty = budget_usd_ / mid;
        qty = round_down(qty, meta_.lot_size);
        if (qty < meta_.min_qty)
        {
            qty = meta_.min_qty; // lift to minimum tradable size
        }

        std::cout << "[MM] " << snapshot.symbol << " mid=" << mid << " live_spread_bps=" << live_spread_bps
                  << " target_spread_bps=" << target_spread_bps << " bid@" << bid_px << " ask@" << ask_px
                  << " qty=" << qty << (live_trading ? " [live]" : " [dry-run]") << "\n";

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

        // Place two quotes per tick; log responses.
        const auto bid_resp =
            helper.submit_limit_order(symbol_, "Buy", to_string_prec(qty), to_string_prec(bid_px), buy_pos_idx_, "Limit", make_link("bid"));
        const auto ask_resp =
            helper.submit_limit_order(symbol_, "Sell", to_string_prec(qty), to_string_prec(ask_px), sell_pos_idx_, "Limit", make_link("ask"));
        std::cout << "[MM][order] bid_resp=" << bid_resp << "\n";
        std::cout << "[MM][order] ask_resp=" << ask_resp << "\n";
    }
    catch (const std::exception &ex)
    {
        std::cerr << "Error processing snapshot for " << snapshot.symbol << ": " << ex.what() << "\n";
    }
}
