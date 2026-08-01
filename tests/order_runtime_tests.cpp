#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "decimal.hpp"
#include "order_reconciler.hpp"
#include "order_resync.hpp"
#include "risk_exit_policy.hpp"
#include "quote_mutation_policy.hpp"
#include "strategy_runtime.hpp"

namespace
{
StrategySettings runtime_settings()
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

TEST_CASE("working orders are amended or selectively cancelled instead of cancel-all recreation")
{
    const std::vector<WorkingOrder> working{
        {"bid_1", "bid-link", "Buy", "Limit", 0.01, 99.0, 1, false, "PostOnly"},
        {"ask_1", "ask-link", "Sell", "Limit", 0.01, 101.0, 2, false, "PostOnly"}};
    const std::vector<PlannedOrder> desired{
        {"bid_1", "Buy", "Limit", 0.01, 98.0, 1, false, "PostOnly"},
        {"tp_long", "Sell", "Limit", 0.02, 102.0, 1, true, "PostOnly"}};

    const auto delta = reconcile_orders(working, desired);

    REQUIRE(delta.amend.size() == 1);
    CHECK(delta.amend[0].order_link_id == "bid-link");
    CHECK(delta.amend[0].desired.price == Catch::Approx(98.0));
    REQUIRE(delta.cancel.size() == 1);
    CHECK(delta.cancel[0].order_link_id == "ask-link");
    REQUIRE(delta.create.size() == 1);
    CHECK(delta.create[0].name == "tp_long");
}

TEST_CASE("order values retain the precision required by tick and lot steps")
{
    CHECK(format_decimal(0.123456789, 0.000000001) == "0.123456789");
    CHECK(format_decimal(100.5, 0.5) == "100.5");
}

TEST_CASE("partially filled owned orders are cancelled before replacement")
{
    const std::vector<WorkingOrder> working{
        {"bid_1", "bid-link", "Buy", "Limit", 0.01, 99.0, 1, false, "PostOnly", "PartiallyFilled"}};
    const std::vector<PlannedOrder> desired{
        {"bid_1", "Buy", "Limit", 0.01, 99.0, 1, false, "PostOnly"}};

    const auto delta = reconcile_orders(working, desired);

    REQUIRE(delta.cancel.size() == 1);
    CHECK(delta.create.empty());
    CHECK(delta.amend.empty());
}

TEST_CASE("cancel acknowledgement never recreates until terminal exchange truth")
{
    const std::vector<WorkingOrder> working{
        {"bid_1", "bid-link", "Buy", "Limit", 0.01, 99.0, 1, false, "PostOnly", "CancelPending"}};
    const std::vector<PlannedOrder> desired{
        {"bid_1", "Buy", "Limit", 0.01, 98.0, 1, false, "PostOnly"}};

    const auto delta = reconcile_orders(working, desired);

    CHECK(delta.cancel.empty());
    CHECK(delta.amend.empty());
    CHECK(delta.create.empty());
}

TEST_CASE("uncertain order truth resync is bounded to six REST reads per minute")
{
    using namespace std::chrono_literals;
    OrderResyncPolicy policy;
    const auto start = OrderResyncPolicy::Clock::time_point{};
    policy.mark_synced(start);
    int reads = 0;

    for (int second = 1; second <= 60; ++second)
    {
        policy.mark_uncertain(start + std::chrono::seconds{second});
        if (policy.due(start + std::chrono::seconds{second}))
        {
            ++reads;
            policy.mark_synced(start + std::chrono::seconds{second});
        }
    }

    CHECK(reads == 6);
}

TEST_CASE("private order events clear uncertainty while periodic resync remains due")
{
    using namespace std::chrono_literals;
    OrderResyncPolicy policy;
    const auto start = OrderResyncPolicy::Clock::time_point{};
    policy.mark_synced(start);
    policy.mark_uncertain(start + 1s);
    policy.mark_event();

    CHECK_FALSE(policy.due(start + 10s));
    CHECK(policy.due(start + 30s));
}

TEST_CASE("owned order links reject logical-name truncation at max counter")
{
    const std::string prefix = "mm_BTCUSDT_12345678_";

    CHECK_THROWS(make_order_link(prefix, "very_long_order_role", UINT64_MAX));
    const auto exact = make_order_link(prefix, "x", UINT64_MAX);
    CHECK(exact.size() <= 36);
    CHECK(exact.rfind(prefix + "x_", 0) == 0);
}

TEST_CASE("runtime kill switch latches after a mark-price stop until restart")
{
    auto settings = runtime_settings();
    settings.stop_loss_bps = 100.0;
    StrategyRuntime runtime("BTCUSDT");
    TradingHelper helper("", "", "linear", "https://api.bybit.com");
    const MarketDataSnapshot snapshot{
        "BTCUSDT",
        {{"symbol", "BTCUSDT"}, {"markPrice", "98"}},
        {{"b", nlohmann::json::array({{"99.5", "1"}})},
         {"a", nlohmann::json::array({{"100.5", "1"}})}}};

    runtime.on_snapshot(settings, snapshot, helper, false,
                        PositionView{0.01, 0.0, 100.0, 0.0});

    CHECK(runtime.kill_latched());
}

TEST_CASE("risk exits retry on a bounded two-second cadence")
{
    using namespace std::chrono_literals;
    RiskExitPolicy policy;
    const auto start = RiskExitPolicy::Clock::time_point{};

    REQUIRE(policy.due(start));
    policy.mark_attempt(start);
    CHECK_FALSE(policy.due(start + 1999ms));
    CHECK(policy.due(start + 2s));
    policy.clear();
    CHECK(policy.due(start + 2s));
}

TEST_CASE("normal quote mutations are capped at five sends per second")
{
    using namespace std::chrono_literals;
    QuoteMutationPolicy policy;
    const auto start = QuoteMutationPolicy::Clock::time_point{};

    REQUIRE(policy.due(start));
    policy.mark_mutated(start);
    CHECK_FALSE(policy.due(start + 199ms));
    CHECK(policy.due(start + 200ms));
}

TEST_CASE("latched risk mode fails closed on nonzero uncloseable dust")
{
    auto settings = runtime_settings();
    settings.stop_loss_bps = 100.0;
    StrategyRuntime runtime("BTCUSDT");
    TradingHelper helper("", "", "linear", "https://api.bybit.com");
    const MarketDataSnapshot breached{
        "BTCUSDT", {{"symbol", "BTCUSDT"}, {"markPrice", "98"}},
        {{"b", nlohmann::json::array({{"99.5", "1"}})},
         {"a", nlohmann::json::array({{"100.5", "1"}})}}};
    runtime.on_snapshot(settings, breached, helper, false,
                        PositionView{0.001, 0.0, 100.0, 0.0});
    const MarketDataSnapshot recovered{
        "BTCUSDT", {{"symbol", "BTCUSDT"}, {"markPrice", "101"}},
        {{"b", nlohmann::json::array({{"100.5", "1"}})},
         {"a", nlohmann::json::array({{"101.5", "1"}})}}};

    CHECK_THROWS_WITH(runtime.on_snapshot(settings, recovered, helper, false,
                                          PositionView{0.0005, 0.0, 100.0, 0.0}),
                      Catch::Matchers::ContainsSubstring("venue minimum"));
}
