#include "strategy_runtime.hpp"

#include "account_state.hpp"
#include "decimal.hpp"
#include "order_batch.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <exception>
#include <stdexcept>
#include <thread>

namespace
{
WorkingOrder as_working(const PlannedOrder &order, const std::string &link_id)
{
    return {order.name, link_id, order.side, order.order_type, order.qty, order.price,
            order.position_idx, order.reduce_only, order.time_in_force, "CreatePending"};
}

constexpr std::array<std::chrono::milliseconds, 3> kCancelBackoff{
    std::chrono::milliseconds{100}, std::chrono::milliseconds{250}, std::chrono::milliseconds{500}};
} // namespace

void StrategyRuntime::cancel_links(TradingHelper &helper, const std::vector<std::string> &links)
{
    OrderRequestBatch requests;
    requests.reserve(links.size());
    for (const auto &link : links)
        requests.push_back(make_cancel_request(symbol_, link));
    for (const auto &batch : split_order_batches(requests))
        helper.batch_cancel_orders(batch);
}

bool StrategyRuntime::confirm_cancellations(TradingHelper &helper, bool require_all_owned_absent)
{
    for (const auto delay : kCancelBackoff)
    {
        std::this_thread::sleep_for(delay);
        const auto orders = parse_open_orders(helper.fetch_open_orders(symbol_), symbol_);
        apply_exchange_truth(orders, true);
        if (cleanup_links_.empty() && (!require_all_owned_absent || working_.empty()))
            return true;
    }
    return false;
}

void StrategyRuntime::refresh_exchange_state(TradingHelper &helper)
{
    const auto orders = parse_open_orders(helper.fetch_open_orders(symbol_), symbol_);
    apply_exchange_truth(orders, true);
    if (!cleanup_links_.empty())
    {
        try
        {
            cancel_links(helper, cleanup_links_);
        }
        catch (...)
        {
            resync_policy_.mark_uncertain(OrderResyncPolicy::Clock::now());
            throw;
        }
        if (!confirm_cancellations(helper, false))
            throw std::runtime_error("owned stale or duplicate orders remain after cancellation retries");
    }
    resync_policy_.mark_synced(OrderResyncPolicy::Clock::now());
}

void StrategyRuntime::execute(const StrategyPlan &plan,
                              const InstrumentMeta &instrument,
                              TradingHelper &helper)
{
    if (plan.stop_triggered)
    {
        if (plan.market_orders.empty())
            throw std::runtime_error("risk stop triggered but venue minimums prevent an exit order");
        const auto now = RiskExitPolicy::Clock::now();
        if (!risk_exit_policy_.due(now))
            return;
        std::exception_ptr first_error;
        std::vector<std::string> links = cleanup_links_;
        for (const auto &order : working_)
            if (pending_cancels_.count(order.order_link_id) == 0)
                links.push_back(order.order_link_id);
        if (!links.empty())
        {
            try
            {
                cancel_links(helper, links);
                for (auto &order : working_)
                {
                    pending_cancels_.insert(order.order_link_id);
                    order.status = "CancelPending";
                }
                cleanup_links_.clear();
            }
            catch (...)
            {
                first_error = std::current_exception();
            }
        }
        for (const auto &order : plan.market_orders)
        {
            try
            {
                helper.submit_market_order(symbol_, order.side,
                                           format_decimal(order.qty, instrument.lot_size),
                                           order.position_idx, make_link(order.name), true);
            }
            catch (...)
            {
                if (!first_error)
                    first_error = std::current_exception();
            }
        }
        risk_exit_policy_.mark_attempt(now);
        resync_policy_.mark_uncertain(OrderResyncPolicy::Clock::now());
        if (first_error)
            std::rethrow_exception(first_error);
        return;
    }
    risk_exit_policy_.clear();
    const auto delta = reconcile_orders(working_, plan.limit_orders);
    if (delta.cancel.empty() && delta.amend.empty() && delta.create.empty())
        return;
    const auto now = QuoteMutationPolicy::Clock::now();
    if (!quote_mutation_policy_.due(now))
        return;
    execute_delta(delta, instrument, helper);
    quote_mutation_policy_.mark_mutated(now);
}

void StrategyRuntime::execute_delta(const OrderDelta &delta,
                                    const InstrumentMeta &instrument,
                                    TradingHelper &helper)
{
    bool mutated = false;
    try
    {
        if (!delta.cancel.empty())
        {
            std::vector<std::string> links;
            for (const auto &order : delta.cancel)
                links.push_back(order.order_link_id);
            cancel_links(helper, links);
            for (const auto &cancelled : delta.cancel)
            {
                pending_cancels_.insert(cancelled.order_link_id);
                const auto current = std::find_if(working_.begin(), working_.end(), [&](const WorkingOrder &order)
                                                  { return order.order_link_id == cancelled.order_link_id; });
                if (current != working_.end())
                    current->status = "CancelPending";
            }
            mutated = true;
        }

        if (!delta.amend.empty())
        {
            OrderRequestBatch requests;
            for (const auto &amend : delta.amend)
                requests.push_back(make_amend_request(
                    symbol_, amend.order_link_id,
                    format_decimal(amend.desired.qty, instrument.lot_size),
                    format_decimal(amend.desired.price, instrument.tick_size)));
            for (const auto &batch : split_order_batches(requests))
                helper.batch_amend_orders(batch);
            for (const auto &amend : delta.amend)
            {
                const auto current = std::find_if(working_.begin(), working_.end(), [&](const WorkingOrder &order)
                                                  { return order.order_link_id == amend.order_link_id; });
                if (current != working_.end())
                {
                    current->status = "AmendPending";
                    pending_amends_[amend.order_link_id] = amend.desired;
                }
            }
            mutated = true;
        }

        if (!delta.create.empty())
        {
            OrderRequestBatch requests;
            std::vector<std::pair<std::string, WorkingOrder>> pending;
            for (const auto &order : delta.create)
            {
                const auto link_id = make_link(order.name);
                requests.push_back(make_create_request(
                    symbol_, order, format_decimal(order.qty, instrument.lot_size),
                    format_decimal(order.price, instrument.tick_size), link_id));
                pending.emplace_back(link_id, as_working(order, link_id));
            }
            for (const auto &batch : split_order_batches(requests))
                helper.batch_submit_orders(batch);
            for (auto &[link_id, order] : pending)
            {
                working_.push_back(order);
                pending_creates_[link_id] = std::move(order);
            }
            mutated = true;
        }
    }
    catch (...)
    {
        resync_policy_.mark_uncertain(OrderResyncPolicy::Clock::now());
        throw;
    }
    if (mutated)
        resync_policy_.mark_uncertain(OrderResyncPolicy::Clock::now());
}

void StrategyRuntime::cancel_working(TradingHelper &helper)
{
    refresh_exchange_state(helper);
    if (working_.empty())
        return;
    std::vector<std::string> links;
    links.reserve(working_.size());
    for (const auto &order : working_)
        links.push_back(order.order_link_id);
    try
    {
        cancel_links(helper, links);
    }
    catch (...)
    {
        resync_policy_.mark_uncertain(OrderResyncPolicy::Clock::now());
        throw;
    }
    for (auto &order : working_)
    {
        pending_cancels_.insert(order.order_link_id);
        order.status = "CancelPending";
    }
    if (!confirm_cancellations(helper, true))
        throw std::runtime_error("owned orders remain after cancellation retries");
    pending_cancels_.clear();
    pending_amends_.clear();
    pending_creates_.clear();
    working_.clear();
}

std::string StrategyRuntime::make_link(const std::string &name)
{
    return make_order_link(owned_order_prefix_, name, ++order_counter_);
}
