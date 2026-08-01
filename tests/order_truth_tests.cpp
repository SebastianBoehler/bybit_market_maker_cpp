#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "strategy_runtime.hpp"

struct StrategyRuntimeTestAccess
{
    static void apply_truth(StrategyRuntime &runtime,
                            const std::vector<OpenOrderView> &orders,
                            bool authoritative)
    {
        runtime.apply_exchange_truth(orders, authoritative);
    }

    static void mark_cancel_pending(StrategyRuntime &runtime, const std::string &link)
    {
        runtime.pending_cancels_.insert(link);
    }

    static void mark_amend_pending(StrategyRuntime &runtime,
                                   const std::string &link,
                                   const PlannedOrder &desired)
    {
        runtime.pending_amends_[link] = desired;
    }

    static void mark_create_pending(StrategyRuntime &runtime,
                                    const std::string &link,
                                    const PlannedOrder &desired)
    {
        WorkingOrder pending{desired.name, link, desired.side, desired.order_type,
                             desired.qty, desired.price, desired.position_idx,
                             desired.reduce_only, desired.time_in_force, "CreatePending"};
        runtime.working_.push_back(pending);
        runtime.pending_creates_[link] = std::move(pending);
    }

    static const std::vector<WorkingOrder> &working(const StrategyRuntime &runtime)
    {
        return runtime.working_;
    }
};

namespace
{
OpenOrderView open_order(const PlannedOrder &order,
                         const std::string &link,
                         const std::string &id)
{
    return {id, link, order.side, order.order_type, order.qty, order.price,
            order.position_idx, order.reduce_only, order.time_in_force, "New", 1};
}
} // namespace

TEST_CASE("authoritative REST truth releases rejected amend and cancel intents for retry")
{
    StrategyRuntime runtime("BTCUSDT");
    const auto prefix = runtime.owned_order_prefix();
    const auto bid_link = prefix + "bid_1_1";
    const auto ask_link = prefix + "ask_1_2";
    const PlannedOrder current_bid{"bid_1", "Buy", "Limit", 0.01, 99.0,
                                   1, false, "PostOnly"};
    const PlannedOrder current_ask{"ask_1", "Sell", "Limit", 0.01, 101.0,
                                   2, false, "PostOnly"};
    const std::vector<OpenOrderView> truth{
        open_order(current_bid, bid_link, "bid-id"),
        open_order(current_ask, ask_link, "ask-id")};
    const PlannedOrder amended_bid{"bid_1", "Buy", "Limit", 0.01, 98.0,
                                   1, false, "PostOnly"};
    StrategyRuntimeTestAccess::apply_truth(runtime, truth, false);
    StrategyRuntimeTestAccess::mark_amend_pending(runtime, bid_link, amended_bid);
    StrategyRuntimeTestAccess::mark_cancel_pending(runtime, ask_link);

    StrategyRuntimeTestAccess::apply_truth(runtime, truth, false);
    const auto &pending = StrategyRuntimeTestAccess::working(runtime);
    CHECK(std::count_if(pending.begin(), pending.end(), [](const WorkingOrder &order)
                        { return order.status == "AmendPending"; }) == 1);
    CHECK(std::count_if(pending.begin(), pending.end(), [](const WorkingOrder &order)
                        { return order.status == "CancelPending"; }) == 1);

    StrategyRuntimeTestAccess::apply_truth(runtime, truth, true);
    const auto delta = reconcile_orders(StrategyRuntimeTestAccess::working(runtime), {amended_bid});
    REQUIRE(delta.amend.size() == 1);
    CHECK(delta.amend.front().order_link_id == bid_link);
    REQUIRE(delta.cancel.size() == 1);
    CHECK(delta.cancel.front().order_link_id == ask_link);
}

TEST_CASE("authoritative partial create truth retries only the missing leg")
{
    StrategyRuntime runtime("BTCUSDT");
    const auto prefix = runtime.owned_order_prefix();
    const PlannedOrder bid{"bid_1", "Buy", "Limit", 0.01, 99.0, 1, false, "PostOnly"};
    const PlannedOrder ask{"ask_1", "Sell", "Limit", 0.01, 101.0, 2, false, "PostOnly"};
    const auto bid_link = prefix + "bid_1_1";
    const auto ask_link = prefix + "ask_1_2";
    StrategyRuntimeTestAccess::mark_create_pending(runtime, bid_link, bid);
    StrategyRuntimeTestAccess::mark_create_pending(runtime, ask_link, ask);

    StrategyRuntimeTestAccess::apply_truth(runtime, {}, false);
    const auto uncertain = reconcile_orders(StrategyRuntimeTestAccess::working(runtime), {bid, ask});
    CHECK(uncertain.create.empty());

    StrategyRuntimeTestAccess::apply_truth(runtime, {open_order(bid, bid_link, "bid-id")}, true);
    const auto resolved = reconcile_orders(StrategyRuntimeTestAccess::working(runtime), {bid, ask});
    REQUIRE(resolved.create.size() == 1);
    CHECK(resolved.create.front().name == "ask_1");
    CHECK(resolved.amend.empty());
    CHECK(resolved.cancel.empty());
}
