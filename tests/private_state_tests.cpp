#include <catch2/catch_test_macros.hpp>

#include "private_state.hpp"

TEST_CASE("private position updates are category and symbol scoped")
{
    PrivateState state("BTCUSDT", "linear", "mm_BTC_");
    state.seed(PositionView{0.01, 0.0, 99.0, 0.0});

    state.apply({{"topic", "position.linear"},
                 {"data", {{{"symbol", "ETHUSDT"},
                            {"positionIdx", 1},
                            {"side", "Buy"},
                            {"size", "9"},
                            {"entryPrice", "2000"}}}}});
    state.apply({{"topic", "position.linear"},
                 {"data", {{{"symbol", "BTCUSDT"},
                            {"positionIdx", 1},
                            {"side", "Buy"},
                            {"size", "0.02"},
                            {"entryPrice", "100"}}}}});

    const auto position = state.position();
    CHECK(position.long_size == 0.02);
    CHECK(position.long_entry == 100.0);
    CHECK(position.short_size == 0.0);
}

TEST_CASE("private readiness needs auth and all valid subscriptions while PnL stays owned-symbol scoped")
{
    PrivateState state("BTCUSDT", "linear", "mm_BTC_");
    state.seed_orders({});
    CHECK_FALSE(state.ready());
    state.apply({{"op", "auth"}, {"success", true}});
    state.apply({{"op", "subscribe"}, {"req_id", "mm-execution"}, {"success", true}});
    state.apply({{"op", "subscribe"}, {"req_id", "mm-position"}, {"success", true}});
    CHECK_FALSE(state.ready());
    state.apply({{"op", "subscribe"}, {"req_id", "mm-order"}, {"success", true}});
    CHECK(state.ready());

    state.apply({{"topic", "execution.linear"},
                 {"data", {{{"symbol", "ETHUSDT"}, {"execId", "eth-1"},
                            {"orderLinkId", "mm_BTC_eth"},
                            {"execPnl", "9"}, {"execFee", "1"}},
                           {{"symbol", "BTCUSDT"}, {"execId", "foreign-1"},
                            {"orderLinkId", "foreign"},
                            {"execPnl", "8"}, {"execFee", "1"}},
                           {{"symbol", "BTCUSDT"}, {"execId", "owned-1"},
                            {"orderLinkId", "mm_BTC_owned"},
                            {"execPnl", "2"}, {"execFee", "0.1"}},
                           {{"symbol", "BTCUSDT"}, {"execId", "owned-1"},
                            {"orderLinkId", "mm_BTC_owned"},
                            {"execPnl", "2"}, {"execFee", "0.1"}}}}});
    state.apply({{"topic", "execution.linear"},
                 {"data", {{{"symbol", "BTCUSDT"}, {"execId", "funding-1"},
                            {"execType", "Funding"}, {"orderLinkId", ""},
                            {"execPnl", "0"}, {"execFee", "0.25"}},
                           {{"symbol", "BTCUSDT"}, {"execId", "funding-1"},
                            {"execType", "Funding"}, {"orderLinkId", ""},
                            {"execPnl", "0"}, {"execFee", "0.25"}}}}});

    const auto pnl = state.pnl();
    CHECK(pnl.realized == 2.0);
    CHECK(pnl.fees == 0.1);
    CHECK(pnl.funding == -0.25);
}

TEST_CASE("private stream processing failures make readiness fail closed")
{
    PrivateState state("BTCUSDT", "linear", "mm_BTC_");
    state.seed_orders({});
    state.apply({{"op", "auth"}, {"success", true}});
    state.apply({{"op", "subscribe"}, {"req_id", "mm-execution"}, {"success", true}});
    state.apply({{"op", "subscribe"}, {"req_id", "mm-position"}, {"success", true}});
    state.apply({{"op", "subscribe"}, {"req_id", "mm-order"}, {"success", true}});
    REQUIRE(state.ready());

    state.mark_unhealthy("malformed position payload");

    CHECK_FALSE(state.healthy());
    CHECK_FALSE(state.ready());
    CHECK(state.error() == "malformed position payload");
}

TEST_CASE("private order updates maintain exact-symbol active exchange truth")
{
    PrivateState state("BTCUSDT", "linear", "mm_BTC_");
    OpenOrderView seeded;
    seeded.order_id = "order-1";
    seeded.order_link_id = "mm_BTC_bid_1";
    seeded.side = "Buy";
    seeded.qty = 0.01;
    seeded.price = 99.0;
    seeded.status = "New";
    state.seed_orders({seeded});
    const auto seed_generation = state.order_snapshot().generation;

    state.apply({{"topic", "order.linear"},
                 {"data", {{{"symbol", "ETHUSDT"}, {"orderId", "other"},
                            {"orderStatus", "New"}, {"leavesQty", "8"}, {"price", "2000"}},
                           {{"symbol", "BTCUSDT"}, {"orderId", "order-1"},
                            {"orderLinkId", "mm_BTC_bid_1"}, {"side", "Buy"},
                            {"orderType", "Limit"}, {"orderStatus", "PartiallyFilled"},
                            {"leavesQty", "0.004"}, {"price", "99"},
                            {"positionIdx", 1}, {"reduceOnly", false},
                            {"timeInForce", "PostOnly"}}}}});

    const auto partial = state.order_snapshot();
    REQUIRE(partial.generation > seed_generation);
    REQUIRE(partial.orders.size() == 1);
    CHECK(partial.orders[0].qty == 0.004);
    CHECK(partial.orders[0].status == "PartiallyFilled");

    state.apply({{"topic", "order.linear"},
                 {"data", {{{"symbol", "BTCUSDT"}, {"orderId", "order-1"},
                            {"orderLinkId", "mm_BTC_bid_1"}, {"orderStatus", "Cancelled"}}}}});

    CHECK(state.order_snapshot().orders.empty());
}

TEST_CASE("combined authenticated resub acknowledgement enables only the required topics")
{
    PrivateState state("BTCUSDT", "linear", "mm_BTC_");
    state.seed_orders({});
    state.apply({{"op", "auth"}, {"success", true}});
    state.apply({{"op", "subscribe"}, {"req_id", "resub"}, {"success", true},
                 {"args", {"execution.linear", "position.linear", "order.linear"}}});

    CHECK(state.ready());
}

TEST_CASE("private account truth rejects unsafe numeric values")
{
    PrivateState state("BTCUSDT", "linear", "mm_BTC_");

    CHECK_THROWS(state.apply({{"topic", "position.linear"},
                              {"data", {{{"symbol", "BTCUSDT"}, {"positionIdx", 1},
                                         {"side", "Buy"}, {"size", "nan"},
                                         {"entryPrice", "100"}}}}}));
    CHECK_THROWS(state.apply({{"topic", "order.linear"},
                              {"data", {{{"symbol", "BTCUSDT"}, {"orderId", "bad"},
                                         {"orderStatus", "New"}, {"leavesQty", "-0.01"},
                                         {"price", "100"}}}}}));
    CHECK_THROWS(state.apply({{"topic", "execution.linear"},
                              {"data", {{{"symbol", "BTCUSDT"}, {"execId", "bad"},
                                         {"orderLinkId", "mm_BTC_bad"},
                                         {"execPnl", "1junk"}, {"execFee", "0.1"}}}}}));
}

TEST_CASE("guarded REST reseed cannot overwrite newer private position or order events")
{
    PrivateState state("BTCUSDT", "linear", "mm_BTC_");
    state.seed(PositionView{0.01, 0.0, 100.0, 0.0});
    state.seed_orders({});
    const auto before_position = state.revisions();

    state.apply({{"topic", "position.linear"},
                 {"data", {{{"symbol", "BTCUSDT"}, {"positionIdx", 1}, {"side", "Buy"},
                            {"size", "0.02"}, {"entryPrice", "101"}}}}});

    CHECK_FALSE(state.seed_if_unchanged(PositionView{0.01, 0.0, 100.0, 0.0}, {}, before_position));
    CHECK(state.position().long_size == 0.02);

    const auto before_order = state.revisions();
    state.apply({{"topic", "order.linear"},
                 {"data", {{{"symbol", "BTCUSDT"}, {"orderId", "new-order"},
                            {"orderLinkId", "manual"}, {"orderStatus", "New"},
                            {"leavesQty", "0.01"}, {"price", "99"}}}}});

    CHECK_FALSE(state.seed_if_unchanged(PositionView{0.02, 0.0, 101.0, 0.0}, {}, before_order));
    REQUIRE(state.order_snapshot().orders.size() == 1);
    CHECK(state.order_snapshot().orders[0].order_id == "new-order");

    const auto stable = state.revisions();
    CHECK(state.seed_if_unchanged(PositionView{0.03, 0.0, 102.0, 0.0}, {}, stable));
    CHECK(state.position().long_size == 0.03);
}

TEST_CASE("multi-leg private positions validate and commit atomically with one revision")
{
    PrivateState state("BTCUSDT", "linear", "mm_BTC_");
    state.seed(PositionView{0.01, 0.01, 100.0, 101.0});
    const auto before = state.revisions();

    CHECK_THROWS(state.apply(
        {{"topic", "position.linear"},
         {"data", {{{"symbol", "BTCUSDT"}, {"positionIdx", 1}, {"side", "Buy"},
                    {"size", "0.02"}, {"entryPrice", "102"}},
                   {{"symbol", "BTCUSDT"}, {"positionIdx", 2}, {"side", "Sell"},
                    {"entryPrice", "103"}}}}}));
    CHECK(state.position().long_size == 0.01);
    CHECK(state.position().short_size == 0.01);
    CHECK(state.revisions().position == before.position);

    state.apply({{"topic", "position.linear"},
                 {"data", {{{"symbol", "BTCUSDT"}, {"positionIdx", 1}, {"side", "Buy"},
                            {"size", "0.02"}, {"entryPrice", "102"}},
                           {{"symbol", "BTCUSDT"}, {"positionIdx", 2}, {"side", "Sell"},
                            {"size", "0"}, {"entryPrice", ""}}}}});

    const auto position = state.position();
    CHECK(position.long_size == 0.02);
    CHECK(position.long_entry == 102.0);
    CHECK(position.short_size == 0.0);
    CHECK(position.short_entry == 0.0);
    CHECK(state.revisions().position == before.position + 1);
}

TEST_CASE("private position rows require explicit entry price even when flat")
{
    PrivateState state("BTCUSDT", "linear", "mm_BTC_");

    CHECK_THROWS(state.apply(
        {{"topic", "position.linear"},
         {"data", {{{"symbol", "BTCUSDT"}, {"positionIdx", 1}, {"side", "Buy"},
                    {"size", "0"}}}}}));
}

TEST_CASE("multi-row private orders reject atomically when a later row is malformed")
{
    PrivateState state("BTCUSDT", "linear", "mm_BTC_");
    state.seed_orders({});
    const auto before = state.order_snapshot();
    const auto revisions = state.revisions();

    CHECK_THROWS(state.apply(
        {{"topic", "order.linear"},
         {"data", {{{"symbol", "BTCUSDT"}, {"orderId", "valid-first"},
                    {"orderLinkId", "manual"}, {"orderStatus", "New"},
                    {"side", "Buy"}, {"leavesQty", "0.01"}, {"price", "99"}},
                   {{"symbol", "BTCUSDT"}, {"orderId", "malformed-second"},
                    {"orderLinkId", "manual-2"}, {"orderStatus", "New"},
                    {"side", "Sell"}, {"leavesQty", "-0.01"}, {"price", "101"}}}}}));

    const auto after = state.order_snapshot();
    CHECK(after.orders.empty());
    CHECK(after.generation == before.generation);
    CHECK(state.revisions().orders == revisions.orders);
}
