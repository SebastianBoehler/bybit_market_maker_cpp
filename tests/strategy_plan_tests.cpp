#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <limits>

#include "market_snapshot.hpp"
#include "strategy_plan.hpp"

namespace
{
const PlannedOrder &order_named(const std::vector<PlannedOrder> &orders, const std::string &name)
{
    const auto it = std::find_if(orders.begin(), orders.end(),
                                 [&](const PlannedOrder &order)
                                 { return order.name == name; });
    REQUIRE(it != orders.end());
    return *it;
}

const PlannedOrder &order_named(const StrategyPlan &plan, const std::string &name)
{
    return order_named(plan.limit_orders, name);
}

StrategySettings base_settings()
{
    StrategySettings settings;
    settings.symbol = "BTCUSDT";
    settings.instrument = {0.01, 0.001, 0.001, 0.01, 100.0, 50.0, 0.01, 1000000.0};
    settings.budget_usd = 1000.0;
    settings.min_spread_bps = 4.0;
    settings.spread_factor = 1.0;
    settings.max_net_qty = 100.0;
    settings.ladder_levels = 1;
    return settings;
}
} // namespace

TEST_CASE("target spread is full spread and prices round passively")
{
    const auto plan = plan_orders(base_settings(), MarketTop{99.99, 100.01, 100.0}, PositionView{}, {});

    REQUIRE(plan.limit_orders.size() == 2);
    CHECK(order_named(plan, "bid_1").price == Catch::Approx(99.98));
    CHECK(order_named(plan, "ask_1").price == Catch::Approx(100.02));
}

TEST_CASE("spread scaling never moves passive quotes inside the current BBO")
{
    auto settings = base_settings();
    settings.min_spread_bps = 0.0;
    settings.spread_factor = 0.25;

    const auto plan = plan_orders(settings, MarketTop{99.0, 101.0, 100.0}, PositionView{}, {});

    CHECK(order_named(plan, "bid_1").price <= 99.0);
    CHECK(order_named(plan, "ask_1").price >= 101.0);
}

TEST_CASE("exact-minimum hedge legs get fee-aware reduce-only take profits on their own indices")
{
    auto settings = base_settings();
    settings.fees = {0.0002, 0.00055};
    settings.tp_safety_bps = 5.0;
    const PositionView position{0.001, 0.001, 100.0, 100.0};

    const auto plan = plan_orders(settings, MarketTop{99.9, 100.1, 100.0}, position, {});
    const auto &long_tp = order_named(plan, "tl0");
    const auto &short_tp = order_named(plan, "ts0");

    CHECK(long_tp.side == "Sell");
    CHECK(long_tp.position_idx == 1);
    CHECK(long_tp.qty == Catch::Approx(0.001));
    CHECK(long_tp.price == Catch::Approx(100.13));
    CHECK(long_tp.reduce_only);
    CHECK(short_tp.side == "Buy");
    CHECK(short_tp.position_idx == 2);
    CHECK(short_tp.qty == Catch::Approx(0.001));
    CHECK(short_tp.price == Catch::Approx(99.87));
    CHECK(short_tp.reduce_only);
}

TEST_CASE("stop exits suppress quotes and close each hedge leg on its own index")
{
    auto settings = base_settings();
    settings.stop_loss_bps = 100.0;
    const PositionView position{0.0029, 0.001, 102.0, 98.0};

    const auto plan = plan_orders(settings, MarketTop{99.9, 100.1, 100.0}, position, {});

    REQUIRE(plan.stop_triggered);
    CHECK(plan.limit_orders.empty());
    REQUIRE(plan.market_orders.size() == 2);
    CHECK(plan.market_orders[0].side == "Sell");
    CHECK(plan.market_orders[0].position_idx == 1);
    CHECK(plan.market_orders[0].qty == Catch::Approx(0.002));
    CHECK(plan.market_orders[0].reduce_only);
    CHECK(plan.market_orders[1].side == "Buy");
    CHECK(plan.market_orders[1].position_idx == 2);
    CHECK(plan.market_orders[1].qty == Catch::Approx(0.001));
    CHECK(plan.market_orders[1].reduce_only);
}

TEST_CASE("one breached hedge leg triggers a strategy-wide flatten")
{
    auto settings = base_settings();
    settings.stop_loss_bps = 100.0;
    const PositionView position{0.002, 0.003, 102.0, 110.0};

    const auto plan = plan_orders(settings, MarketTop{99.9, 100.1, 100.0}, position, {});

    REQUIRE(plan.stop_triggered);
    REQUIRE(plan.market_orders.size() == 2);
    CHECK(order_named(plan.market_orders, "stop_long").position_idx == 1);
    CHECK(order_named(plan.market_orders, "stop_short").position_idx == 2);
}

TEST_CASE("inventory-scaled opening quantities floor to lot size within total budget")
{
    auto settings = base_settings();
    settings.instrument = {0.01, 0.01, 0.01, 0.01, 100.0, 50.0, 0.01, 1000000.0};
    settings.budget_usd = 10.0;
    settings.max_net_qty = 0.08;
    const PositionView position{0.03, 0.0, 0.0, 0.0};

    const auto plan = plan_orders(settings, MarketTop{99.99, 100.01, 100.0}, position, {});
    const auto &bid = order_named(plan, "bid_1");
    const auto &ask = order_named(plan, "ask_1");

    CHECK(bid.qty == Catch::Approx(0.03));
    CHECK(ask.qty == Catch::Approx(0.04));
    CHECK(bid.qty * bid.price + ask.qty * ask.price <= 10.0);
}

TEST_CASE("gross cap counts positions plus resting and intended opening orders")
{
    auto settings = base_settings();
    settings.instrument = {0.01, 0.01, 0.01, 0.01, 100.0, 50.0, 0.01, 1000000.0};
    settings.budget_usd = 10.0;
    settings.gross_notional_cap = 8.0;
    const PositionView position{0.03, 0.0, 0.0, 0.0};
    const std::vector<RestingOrder> resting{{"external", 0.02, 100.0, false, "Buy"}};

    const auto plan = plan_orders(settings, MarketTop{99.99, 100.01, 100.0}, position, resting);

    REQUIRE(plan.limit_orders.size() == 1);
    CHECK(plan.limit_orders[0].name == "bid_1");
    CHECK((position.long_size + position.short_size) * 100.0 + 2.0 +
              plan.limit_orders[0].qty * plan.limit_orders[0].price <=
          8.0);
}

TEST_CASE("hard net cap includes same-direction resting and intended ladder quantities")
{
    auto settings = base_settings();
    settings.instrument = {0.01, 0.01, 0.01, 0.01, 100.0, 50.0, 0.01, 1000000.0};
    settings.budget_usd = 10.0;
    settings.max_net_qty = 0.05;
    const PositionView position{0.02, 0.0, 0.0, 0.0};
    const std::vector<RestingOrder> resting{{"external_bid", 0.02, 99.0, false, "Buy"}};

    const auto plan = plan_orders(settings, MarketTop{99.99, 100.01, 100.0}, position, resting);
    const auto &bid = order_named(plan, "bid_1");

    CHECK(bid.qty == Catch::Approx(0.01));
    CHECK(position.long_size - position.short_size + resting[0].qty + bid.qty <= settings.max_net_qty);
}

TEST_CASE("reducing an existing quote does not fund another leg before venue confirmation")
{
    auto settings = base_settings();
    settings.instrument = {0.01, 0.01, 0.01, 0.01, 100.0, 50.0, 0.01, 1000000.0};
    settings.mode = StrategyMode::LongOnly;
    settings.budget_usd = 10.0;
    settings.max_net_qty = 0.10;
    settings.gross_notional_cap = 10.0;
    settings.ladder_levels = 2;
    const RestingOrder old_bid{"bid_1", 0.08, 99.0, false, "Buy"};

    const auto plan = plan_orders(settings, MarketTop{99.99, 100.01, 100.0},
                                  PositionView{}, {old_bid});
    const auto &replacement = order_named(plan, "bid_1");
    const auto &new_leg = order_named(plan, "bid_2");

    CHECK(replacement.qty < old_bid.qty);
    CHECK(old_bid.qty + new_leg.qty <= settings.max_net_qty + 1e-12);
    CHECK(old_bid.qty * old_bid.price + new_leg.qty * new_leg.price <=
          settings.gross_notional_cap + 1e-12);
}

TEST_CASE("stop risk uses mark price instead of quote mid")
{
    auto settings = base_settings();
    settings.stop_loss_bps = 100.0;
    const PositionView position{0.01, 0.0, 100.0, 0.0};

    const auto plan = plan_orders(settings, MarketTop{99.5, 100.5, 98.0}, position, {});

    REQUIRE(plan.stop_triggered);
    REQUIRE(plan.market_orders.size() == 1);
    CHECK(plan.market_orders[0].side == "Sell");
}

TEST_CASE("venue notional and max quantity constrain planned orders")
{
    auto settings = base_settings();
    settings.instrument.min_notional = 5.0;
    settings.instrument.max_limit_qty = 0.06;
    settings.budget_usd = 1000.0;

    const auto capped = plan_orders(settings, MarketTop{99.99, 100.01, 100.0}, PositionView{}, {});
    REQUIRE(capped.limit_orders.size() == 2);
    CHECK(capped.limit_orders[0].qty <= 0.06);
    CHECK(capped.limit_orders[1].qty <= 0.06);

    settings.budget_usd = 1.0;
    const auto too_small = plan_orders(settings, MarketTop{99.99, 100.01, 100.0}, PositionView{}, {});
    CHECK(too_small.limit_orders.empty());
}

TEST_CASE("reduce-only risk exit preserves the full close quantity above venue max")
{
    auto settings = base_settings();
    settings.stop_loss_bps = 100.0;
    settings.instrument.max_market_qty = 0.02;
    const PositionView position{0.05, 0.0, 100.0, 0.0};

    const auto plan = plan_orders(settings, MarketTop{99.5, 100.5, 98.0}, position, {});

    REQUIRE(plan.market_orders.size() == 1);
    CHECK(plan.market_orders[0].qty == Catch::Approx(0.05));
}

TEST_CASE("long-only opening mode still protects a pre-existing short hedge leg")
{
    auto settings = base_settings();
    settings.mode = StrategyMode::LongOnly;
    settings.stop_loss_bps = 100.0;
    const PositionView position{0.0, 0.01, 0.0, 100.0};

    const auto stopped = plan_orders(settings, MarketTop{99.5, 100.5, 102.0}, position, {});

    REQUIRE(stopped.stop_triggered);
    REQUIRE(stopped.market_orders.size() == 1);
    CHECK(stopped.market_orders[0].side == "Buy");
    CHECK(stopped.market_orders[0].position_idx == 2);
}

TEST_CASE("market parser and planner reject non-finite or partially parsed prices")
{
    MarketDataSnapshot snapshot{
        "BTCUSDT",
        {{"symbol", "BTCUSDT"}, {"markPrice", "100"}},
        {{"b", nlohmann::json::array({{"99junk", "1"}})},
         {"a", nlohmann::json::array({{"101", "1"}})}}};

    CHECK_THROWS(parse_market_top(snapshot));
    snapshot.orderbook["b"][0][0] = "nan";
    CHECK_THROWS(parse_market_top(snapshot));
    CHECK_THROWS(plan_orders(base_settings(),
                             MarketTop{std::numeric_limits<double>::quiet_NaN(), 101.0, 100.0},
                             PositionView{}, {}));
    CHECK_THROWS(plan_orders(base_settings(),
                             MarketTop{99.0, std::numeric_limits<double>::infinity(), 100.0},
                             PositionView{}, {}));
}

TEST_CASE("planner rejects zero-valued resting exposure and nonsensical stop thresholds")
{
    CHECK_THROWS(plan_orders(base_settings(), MarketTop{99.0, 101.0, 100.0}, PositionView{},
                             {RestingOrder{"bad", 0.0, 100.0, false, "Buy"}}));

    auto settings = base_settings();
    settings.stop_loss_bps = 10000.0;
    CHECK_THROWS(plan_orders(settings, MarketTop{99.0, 101.0, 100.0}, PositionView{}, {}));
}

TEST_CASE("latched risk mode forces remaining positions closed after mark recovery")
{
    auto settings = base_settings();
    settings.stop_loss_bps = 100.0;

    const auto plan = plan_orders(settings, MarketTop{99.0, 101.0, 101.0},
                                  PositionView{0.01, 0.0, 100.0, 0.0}, {}, true);

    REQUIRE(plan.stop_triggered);
    REQUIRE(plan.market_orders.size() == 1);
    CHECK(plan.market_orders[0].name == "stop_long");
}
