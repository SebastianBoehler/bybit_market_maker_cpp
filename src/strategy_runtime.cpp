#include "strategy_runtime.hpp"

#include "account_state.hpp"
#include "market_snapshot.hpp"

#include <chrono>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace
{
std::string session_text(std::uint64_t session_id)
{
    std::ostringstream output;
    output << std::setw(8) << std::setfill('0') << session_id;
    return output.str();
}

bool amended(const WorkingOrder &working, const PlannedOrder &desired)
{
    return std::abs(working.qty - desired.qty) <= 1e-12 &&
           std::abs(working.price - desired.price) <= 1e-12 && working.status == "New";
}
} // namespace

StrategyRuntime::StrategyRuntime(std::string symbol, std::vector<RestingOrder> external_resting)
    : symbol_(std::move(symbol)),
      session_id_(static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                               std::chrono::system_clock::now().time_since_epoch())
                                               .count()) %
                  100000000ULL),
      application_order_prefix_("mm_" + symbol_.substr(0, 8) + "_"),
      owned_order_prefix_(application_order_prefix_ + session_text(session_id_) + "_"),
      external_resting_(std::move(external_resting))
{
    for (auto &order : external_resting_)
        if (order.name.rfind("external:", 0) != 0)
            order.name = "external:" + order.name;
}

void StrategyRuntime::sync_open_orders(const OpenOrderSnapshot &snapshot)
{
    if (!snapshot.seeded)
        throw std::runtime_error("open-order state was not bootstrapped from REST");
    if (orders_seeded_ && snapshot.generation <= private_order_generation_)
        return;
    apply_exchange_truth(snapshot.orders, false);
    private_order_generation_ = snapshot.generation;
    if (!orders_seeded_)
        resync_policy_.mark_synced(OrderResyncPolicy::Clock::now());
    else
        resync_policy_.mark_event();
    orders_seeded_ = true;
}

void StrategyRuntime::apply_exchange_truth(const std::vector<OpenOrderView> &orders, bool authoritative)
{
    if (authoritative)
    {
        pending_cancels_.clear();
        pending_amends_.clear();
    }
    auto classified = classify_open_orders(orders, application_order_prefix_, owned_order_prefix_);
    std::unordered_set<std::string> live_links;
    for (auto &order : classified.working)
    {
        live_links.insert(order.order_link_id);
        if (pending_cancels_.count(order.order_link_id) != 0)
        {
            order.status = "CancelPending";
            continue;
        }
        const auto amend = pending_amends_.find(order.order_link_id);
        if (amend != pending_amends_.end())
        {
            if (amended(order, amend->second))
                pending_amends_.erase(amend);
            else
                order.status = "AmendPending";
        }
        if (pending_creates_.erase(order.order_link_id) != 0 && order.status != "New")
            order.status = "CreatePending";
    }

    for (auto it = pending_cancels_.begin(); it != pending_cancels_.end();)
        it = live_links.count(*it) == 0 ? pending_cancels_.erase(it) : std::next(it);
    for (auto it = pending_amends_.begin(); it != pending_amends_.end();)
        it = live_links.count(it->first) == 0 ? pending_amends_.erase(it) : std::next(it);
    for (auto it = pending_creates_.begin(); it != pending_creates_.end();)
    {
        if (live_links.count(it->first) != 0 || authoritative)
        {
            it = pending_creates_.erase(it);
            continue;
        }
        auto pending = it->second;
        pending.status = "CreatePending";
        classified.working.push_back(std::move(pending));
        ++it;
    }

    working_ = std::move(classified.working);
    external_resting_ = std::move(classified.external);
    cleanup_links_ = std::move(classified.cancel_links);
}

void StrategyRuntime::on_snapshot(const StrategySettings &settings,
                                  const MarketDataSnapshot &snapshot,
                                  TradingHelper &helper,
                                  bool live_trading,
                                  const PositionView &position)
{
    if (snapshot.symbol != symbol_ || settings.symbol != symbol_)
        throw std::runtime_error("strategy received a snapshot for the wrong symbol");
    const auto market = parse_market_top(snapshot);
    auto plan = plan_orders(settings, market, position, resting_exposure());
    if (plan.stop_triggered)
        kill_latched_ = true;
    if (kill_latched_)
        plan = plan_orders(settings, market, position, resting_exposure(), true);
    if (plan.stop_triggered && plan.market_orders.empty())
        throw std::runtime_error("risk position remains below the venue minimum exit quantity");
    if (!live_trading || !helper.has_credentials())
        return;
    if (!orders_seeded_)
        throw std::runtime_error("live trading requires REST-seeded open-order truth");
    if (kill_latched_ && !plan.stop_triggered)
    {
        if (!cleanup_links_.empty() || resync_policy_.due(OrderResyncPolicy::Clock::now()))
            refresh_exchange_state(helper);
        execute(StrategyPlan{}, settings.instrument, helper);
        return;
    }
    if (!plan.stop_triggered)
    {
        if (!cleanup_links_.empty() || resync_policy_.due(OrderResyncPolicy::Clock::now()))
            refresh_exchange_state(helper);
        plan = plan_orders(settings, market, position, resting_exposure());
    }
    execute(plan, settings.instrument, helper);
}

std::vector<RestingOrder> StrategyRuntime::resting_exposure() const
{
    auto exposure = external_resting_;
    for (const auto &order : working_)
        exposure.push_back({order.name, order.qty, order.price, order.reduce_only, order.side});
    return exposure;
}
