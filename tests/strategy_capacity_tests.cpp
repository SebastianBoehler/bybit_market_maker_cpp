#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <set>

#include "account_state.hpp"
#include "order_reconciler.hpp"
#include "strategy_plan.hpp"

namespace
{
StrategySettings capacity_settings()
{
    StrategySettings settings;
    settings.symbol = "BTCUSDT";
    settings.instrument = {0.01, 0.01, 0.01, 0.01, 100.0, 50.0, 0.01, 1000000.0};
    settings.mode = StrategyMode::LongOnly;
    settings.budget_usd = 10.0;
    settings.min_spread_bps = 4.0;
    settings.spread_factor = 1.0;
    settings.max_net_qty = 100.0;
    settings.gross_notional_cap = 10.0;
    settings.ladder_levels = 1;
    return settings;
}

const PlannedOrder &order_named(const StrategyPlan &plan, const std::string &name)
{
    const auto found = std::find_if(plan.limit_orders.begin(), plan.limit_orders.end(),
                                    [&](const PlannedOrder &order)
                                    { return order.name == name; });
    REQUIRE(found != plan.limit_orders.end());
    return *found;
}
} // namespace

TEST_CASE("wrong-side same-name exposure is not reusable replacement notional")
{
    const auto settings = capacity_settings();
    const RestingOrder wrong_side{"bid_1", 0.08, 99.0, false, "Sell"};

    const auto plan = plan_orders(settings, MarketTop{99.99, 100.01, 100.0},
                                  PositionView{}, {wrong_side});
    const auto &bid = order_named(plan, "bid_1");

    CHECK(wrong_side.qty * wrong_side.price + bid.qty * bid.price <=
          settings.gross_notional_cap + 1e-12);
}

TEST_CASE("oversized hedge take profits use deterministic venue-sized chunks")
{
    auto settings = capacity_settings();
    settings.budget_usd = 0.0;
    settings.instrument.lot_size = 0.001;
    settings.instrument.min_qty = 0.003;
    settings.instrument.max_limit_qty = 0.01;
    const auto plan = plan_orders(settings, MarketTop{99.9, 100.1, 100.0},
                                  PositionView{0.0255, 0.0214, 100.0, 100.0}, {});

    std::vector<PlannedOrder> exits;
    std::set<std::string> names;
    for (const auto &order : plan.limit_orders)
    {
        if (!order.reduce_only)
            continue;
        exits.push_back(order);
        names.insert(order.name);
        CHECK(order.qty >= settings.instrument.min_qty);
        CHECK(order.qty <= settings.instrument.max_limit_qty);
    }

    REQUIRE(exits.size() == 6);
    CHECK(names.size() == exits.size());
    CHECK(exits[0].name == "tl0");
    CHECK(exits[1].name == "tl1");
    CHECK(exits[2].name == "tl2");
    CHECK(exits[0].qty == Catch::Approx(0.01));
    CHECK(exits[1].qty == Catch::Approx(0.01));
    CHECK(exits[2].qty == Catch::Approx(0.005));
    CHECK(exits[3].name == "ts0");
    CHECK(exits[4].name == "ts1");
    CHECK(exits[5].name == "ts2");
    CHECK(exits[3].qty == Catch::Approx(0.01));
    CHECK(exits[4].qty == Catch::Approx(0.008));
    CHECK(exits[5].qty == Catch::Approx(0.003));
}

TEST_CASE("take-profit chunks reconcile by stable unique names as positions shrink")
{
    auto settings = capacity_settings();
    settings.budget_usd = 0.0;
    settings.instrument.lot_size = 0.001;
    settings.instrument.min_qty = 0.003;
    settings.instrument.max_limit_qty = 0.01;
    const auto initial = plan_orders(settings, MarketTop{99.9, 100.1, 100.0},
                                     PositionView{0.025, 0.0, 100.0, 0.0}, {});
    std::vector<WorkingOrder> working;
    for (std::size_t index = 0; index < initial.limit_orders.size(); ++index)
    {
        const auto &order = initial.limit_orders[index];
        working.push_back({order.name, "link-" + std::to_string(index), order.side,
                           order.order_type, order.qty, order.price, order.position_idx,
                           order.reduce_only, order.time_in_force});
    }
    const auto smaller = plan_orders(settings, MarketTop{99.9, 100.1, 100.0},
                                     PositionView{0.015, 0.0, 100.0, 0.0}, {});

    const auto delta = reconcile_orders(working, smaller.limit_orders);

    REQUIRE(delta.amend.size() == 1);
    CHECK(delta.amend.front().desired.name == "tl1");
    CHECK(delta.amend.front().desired.qty == Catch::Approx(0.005));
    REQUIRE(delta.cancel.size() == 1);
    CHECK(delta.cancel.front().name == "tl2");
    CHECK(delta.create.empty());
}

TEST_CASE("maximum take-profit plan names round-trip through widened owned links")
{
    auto settings = capacity_settings();
    settings.budget_usd = 0.0;
    settings.instrument.lot_size = 0.001;
    settings.instrument.min_qty = 0.001;
    settings.instrument.max_limit_qty = 0.001;
    const auto plan = plan_orders(settings, MarketTop{99.9, 100.1, 100.0},
                                  PositionView{0.5, 0.0, 100.0, 0.0}, {});
    const std::string application_prefix = "mm_ABCDEFGH_";
    const std::string session_prefix = application_prefix + "12345678_";
    constexpr std::uint64_t widened_counter = 78364164096ULL;
    std::vector<OpenOrderView> exchange_orders;
    for (std::size_t index = 0; index < plan.limit_orders.size(); ++index)
    {
        const auto &order = plan.limit_orders[index];
        exchange_orders.push_back({"id-" + std::to_string(index),
                                   make_order_link(session_prefix, order.name,
                                                   widened_counter + index),
                                   order.side, order.order_type, order.qty, order.price,
                                   order.position_idx, order.reduce_only,
                                   order.time_in_force, "New", index});
    }

    REQUIRE(plan.limit_orders.size() == 500);
    CHECK(plan.limit_orders.front().name == "tl0");
    CHECK(plan.limit_orders.back().name == "tldv");
    const auto classified = classify_open_orders(exchange_orders, application_prefix, session_prefix);
    CHECK(classified.cancel_links.empty());
    const auto delta = reconcile_orders(classified.working, plan.limit_orders);
    CHECK(delta.create.empty());
    CHECK(delta.amend.empty());
    CHECK(delta.cancel.empty());
}

TEST_CASE("persistent order ceiling is fail-closed across exits ladder and external truth")
{
    auto settings = capacity_settings();
    settings.budget_usd = 0.0;
    settings.instrument.lot_size = 0.001;
    settings.instrument.min_qty = 0.001;
    settings.instrument.max_limit_qty = 0.001;
    CHECK_THROWS(plan_orders(settings, MarketTop{99.9, 100.1, 100.0},
                             PositionView{0.25, 0.251, 100.0, 100.0}, {}));

    settings.mode = StrategyMode::Both;
    settings.budget_usd = 1000.0;
    std::vector<RestingOrder> external(499,
                                       {"external:manual", 0.001, 100.0, true, "Sell"});
    CHECK_THROWS(plan_orders(settings, MarketTop{99.9, 100.1, 100.0},
                             PositionView{}, external));
}
