#include "strategy_plan.hpp"

#include "base36.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace
{
double floor_step(double value, double step)
{
    if (step <= 0.0)
        return value;
    return std::floor(value / step + 1e-9) * step;
}

double ceil_step(double value, double step)
{
    if (step <= 0.0)
        return value;
    return std::ceil(value / step - 1e-9) * step;
}
} // namespace

StrategyPlan plan_orders(const StrategySettings &settings,
                         MarketTop market,
                         const PositionView &position,
                         const std::vector<RestingOrder> &resting_orders,
                         bool force_exit)
{
    StrategyPlan plan;
    if (!std::isfinite(market.bid) || !std::isfinite(market.ask) ||
        !std::isfinite(market.risk_price) || market.bid <= 0.0 ||
        market.ask <= market.bid || market.risk_price <= 0.0)
        throw std::runtime_error("market top must contain finite positive crossed-safe prices");
    const double position_values[] = {position.long_size, position.short_size,
                                      position.long_entry, position.short_entry};
    if (!std::all_of(std::begin(position_values), std::end(position_values),
                     [](double value)
                     { return std::isfinite(value) && value >= 0.0; }))
        throw std::runtime_error("position truth contains unsafe numeric values");
    for (const auto &order : resting_orders)
        if (!std::isfinite(order.qty) || !std::isfinite(order.price) ||
            order.qty <= 0.0 || order.price <= 0.0)
            throw std::runtime_error("resting-order truth contains unsafe numeric values");
    if (settings.stop_loss_bps >= 10000.0)
        throw std::runtime_error("enabled stop loss must be below 10000 bps");
    if (settings.ladder_levels <= 0)
        return plan;

    constexpr std::size_t kMaxPersistentOrdersPerSymbol = 500;
    const auto external_order_count = static_cast<std::size_t>(std::count_if(
        resting_orders.begin(), resting_orders.end(), [](const RestingOrder &order)
        { return order.name.rfind("external:", 0) == 0; }));
    auto require_order_capacity = [&](std::size_t additional)
    {
        if (external_order_count > kMaxPersistentOrdersPerSymbol ||
            plan.limit_orders.size() > kMaxPersistentOrdersPerSymbol - external_order_count ||
            additional > kMaxPersistentOrdersPerSymbol - external_order_count - plan.limit_orders.size())
            throw std::runtime_error("persistent orders exceed Bybit's per-symbol active-order limit");
    };

    const double mid = 0.5 * (market.bid + market.ask);
    auto append_market_exit = [&](const std::string &name, const std::string &side,
                                  double total_qty, int position_idx)
    {
        const double qty = floor_step(total_qty, settings.instrument.lot_size);
        if (qty >= settings.instrument.min_qty)
            plan.market_orders.push_back({name, side, "Market", qty, 0.0,
                                          position_idx, true, "IOC"});
    };
    if (force_exit)
    {
        append_market_exit("stop_long", "Sell", position.long_size, settings.long_position_idx);
        append_market_exit("stop_short", "Buy", position.short_size, settings.short_position_idx);
        plan.stop_triggered = position.long_size > 0.0 || position.short_size > 0.0;
        return plan;
    }
    if (settings.stop_loss_bps > 0.0)
    {
        const double stop_fraction = settings.stop_loss_bps * 0.0001;
        const double long_qty = floor_step(position.long_size, settings.instrument.lot_size);
        const double short_qty = floor_step(position.short_size, settings.instrument.lot_size);
        const bool long_breached = long_qty >= settings.instrument.min_qty && position.long_entry > 0.0 &&
                                   market.risk_price <= position.long_entry * (1.0 - stop_fraction);
        const bool short_breached = short_qty >= settings.instrument.min_qty && position.short_entry > 0.0 &&
                                    market.risk_price >= position.short_entry * (1.0 + stop_fraction);
        if (long_breached || short_breached)
        {
            append_market_exit("stop_long", "Sell", long_qty, settings.long_position_idx);
            append_market_exit("stop_short", "Buy", short_qty, settings.short_position_idx);
            plan.stop_triggered = true;
            return plan;
        }
    }

    const double live_spread_bps = (market.ask - market.bid) / mid * 1e4;
    const double fee_floor_bps = 2.0 * std::max(0.0, settings.fees.maker) * 1e4;
    const double full_spread_bps = std::max({live_spread_bps,
                                             live_spread_bps * settings.spread_factor,
                                             settings.min_spread_bps + fee_floor_bps});
    const double half_spread = mid * full_spread_bps * 0.0001 * 0.5;
    const int sides = settings.mode == StrategyMode::Both ? 2 : 1;
    const int legs = sides * settings.ladder_levels;
    const double budget_per_leg = settings.budget_usd / legs;
    const double net_qty = position.long_size - position.short_size;
    double bid_scale = 1.0;
    double ask_scale = 1.0;
    if (settings.max_net_qty > 0.0)
    {
        if (net_qty >= settings.max_net_qty)
            bid_scale = 0.0;
        else if (net_qty > 0.0)
            bid_scale = 1.0 - net_qty / settings.max_net_qty;
        if (net_qty <= -settings.max_net_qty)
            ask_scale = 0.0;
        else if (net_qty < 0.0)
            ask_scale = 1.0 - std::abs(net_qty) / settings.max_net_qty;
    }

    struct ExistingExposure
    {
        double buy_qty{0.0};
        double sell_qty{0.0};
        double notional{0.0};
        bool unknown_side{false};
    };
    std::unordered_map<std::string, ExistingExposure> existing;
    double gross = (position.long_size + position.short_size) * market.risk_price;
    double buy_capacity = settings.max_net_qty > 0.0
                              ? settings.max_net_qty - net_qty
                              : std::numeric_limits<double>::infinity();
    double sell_capacity = settings.max_net_qty > 0.0
                               ? settings.max_net_qty + net_qty
                               : std::numeric_limits<double>::infinity();
    for (const auto &order : resting_orders)
    {
        if (order.reduce_only)
            continue;
        const double notional = order.qty * order.price;
        gross += notional;
        auto &exposure = existing[order.name];
        exposure.notional += notional;
        if (order.side == "Buy")
        {
            exposure.buy_qty += order.qty;
            buy_capacity -= order.qty;
        }
        else if (order.side == "Sell")
        {
            exposure.sell_qty += order.qty;
            sell_capacity -= order.qty;
        }
        else
        {
            exposure.unknown_side = true;
            buy_capacity -= order.qty;
            sell_capacity -= order.qty;
        }
    }

    auto add_opening_order = [&](std::string name, std::string side, double raw_qty,
                                 double price, int position_idx)
    {
        auto old = existing.find(name);
        double reusable_qty = 0.0;
        double reusable_notional = 0.0;
        if (old != existing.end())
        {
            const bool same_side_only = !old->second.unknown_side &&
                                        (side == "Buy" ? old->second.sell_qty == 0.0
                                                       : old->second.buy_qty == 0.0);
            if (same_side_only)
            {
                reusable_qty = side == "Buy" ? old->second.buy_qty : old->second.sell_qty;
                reusable_notional = old->second.notional;
            }
            existing.erase(old);
        }
        double &directional_capacity = side == "Buy" ? buy_capacity : sell_capacity;

        double qty = raw_qty;
        if (std::isfinite(directional_capacity))
            qty = std::min(qty, floor_step(std::max(0.0, directional_capacity + reusable_qty),
                                           settings.instrument.lot_size));
        if (settings.gross_notional_cap > 0.0)
        {
            const double notional_capacity = std::max(
                0.0, settings.gross_notional_cap - gross + reusable_notional);
            qty = std::min(qty, floor_step(notional_capacity / price, settings.instrument.lot_size));
        }
        qty = floor_step(qty, settings.instrument.lot_size);
        qty = std::min(qty, settings.instrument.max_limit_qty);
        qty = floor_step(qty, settings.instrument.lot_size);
        if (qty < settings.instrument.min_qty ||
            qty * price + 1e-12 < settings.instrument.min_notional ||
            price < settings.instrument.min_price || price > settings.instrument.max_price)
            return;
        require_order_capacity(1);
        plan.limit_orders.push_back({std::move(name), side, "Limit", qty, price,
                                     position_idx, false, "PostOnly"});
        directional_capacity -= std::max(0.0, qty - reusable_qty);
        gross += std::max(0.0, qty * price - reusable_notional);
    };

    for (int level = 1; level <= settings.ladder_levels; ++level)
    {
        const double offset = half_spread * level;
        const double bid_price = floor_step(mid - offset, settings.instrument.tick_size);
        const double bid_qty = floor_step(budget_per_leg / bid_price * bid_scale, settings.instrument.lot_size);
        add_opening_order("bid_" + std::to_string(level), "Buy", bid_qty, bid_price,
                          settings.long_position_idx);
        if (settings.mode == StrategyMode::Both)
        {
            const double ask_price = ceil_step(mid + offset, settings.instrument.tick_size);
            const double ask_qty = floor_step(budget_per_leg / ask_price * ask_scale, settings.instrument.lot_size);
            add_opening_order("ask_" + std::to_string(level), "Sell", ask_qty, ask_price,
                              settings.short_position_idx);
        }
    }

    const double entry_fee = std::max(0.0, settings.fees.taker);
    const double exit_fee = std::max(0.0, settings.fees.maker);
    const double safety = std::max(0.0, settings.tp_safety_bps) * 0.0001;
    auto append_limit_exit = [&](const std::string &name, const std::string &side,
                                 double total_qty, double price, int position_idx)
    {
        if (price < settings.instrument.min_price || price > settings.instrument.max_price)
            return;
        double remaining = floor_step(total_qty, settings.instrument.lot_size);
        if (remaining < settings.instrument.min_qty)
            return;
        const double min_chunk = ceil_step(settings.instrument.min_qty,
                                           settings.instrument.lot_size);
        const double max_chunk = floor_step(settings.instrument.max_limit_qty,
                                            settings.instrument.lot_size);
        if (max_chunk < min_chunk)
            throw std::runtime_error("venue take-profit quantity bounds cannot form an order");
        const double raw_count = std::ceil(remaining / max_chunk - 1e-9);
        if (!std::isfinite(raw_count) || raw_count < 1.0 ||
            raw_count > static_cast<double>(kMaxPersistentOrdersPerSymbol))
            throw std::runtime_error("take-profit chunk count is unsafe");
        const auto chunk_count = static_cast<std::size_t>(raw_count);
        if (remaining + 1e-12 < static_cast<double>(chunk_count) * min_chunk)
            throw std::runtime_error("position cannot be split within venue take-profit bounds");
        require_order_capacity(chunk_count);
        for (std::size_t index = 1; index <= chunk_count; ++index)
        {
            const auto chunks_left = chunk_count - index;
            const double reserved = static_cast<double>(chunks_left) * min_chunk;
            const double qty = floor_step(std::min(max_chunk, remaining - reserved),
                                          settings.instrument.lot_size);
            if (qty < min_chunk || qty > max_chunk)
                throw std::runtime_error("take-profit chunk violates venue quantity bounds");
            const auto chunk_name = name + encode_base36(index - 1);
            plan.limit_orders.push_back({chunk_name, side, "Limit", qty, price,
                                         position_idx, true, "PostOnly"});
            remaining = floor_step(std::max(0.0, remaining - qty),
                                   settings.instrument.lot_size);
        }
        if (remaining > 1e-12)
            throw std::runtime_error("take-profit chunks do not cover the rounded position");
    };

    const double long_qty = floor_step(position.long_size, settings.instrument.lot_size);
    if (long_qty >= settings.instrument.min_qty && position.long_entry > 0.0 && exit_fee < 1.0)
    {
        const double economic_floor = position.long_entry * (1.0 + entry_fee + safety) / (1.0 - exit_fee);
        append_limit_exit("tl", "Sell", long_qty,
                          ceil_step(std::max(market.ask, economic_floor), settings.instrument.tick_size),
                          settings.long_position_idx);
    }
    const double short_qty = floor_step(position.short_size, settings.instrument.lot_size);
    if (short_qty >= settings.instrument.min_qty &&
        position.short_entry > 0.0)
    {
        const double economic_ceiling = position.short_entry * (1.0 - entry_fee - safety) / (1.0 + exit_fee);
        append_limit_exit("ts", "Buy", short_qty,
                          floor_step(std::min(market.bid, economic_ceiling), settings.instrument.tick_size),
                          settings.short_position_idx);
    }
    return plan;
}
