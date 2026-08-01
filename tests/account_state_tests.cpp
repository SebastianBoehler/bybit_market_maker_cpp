#include <algorithm>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "account_state.hpp"

namespace
{
OpenOrderView open_order(std::string link, std::string side, double qty, double price, int position_idx)
{
    OpenOrderView order;
    order.order_link_id = std::move(link);
    order.side = std::move(side);
    order.order_type = "Limit";
    order.qty = qty;
    order.price = price;
    order.position_idx = position_idx;
    order.time_in_force = "PostOnly";
    order.status = "New";
    return order;
}
} // namespace

TEST_CASE("REST position bootstrap is exact-symbol scoped and uses hedge indices")
{
    const nlohmann::json response = {
        {"retCode", 0},
        {"result", {{"list", {
            {{"symbol", "ETHUSDT"}, {"positionIdx", 1}, {"size", "9"}, {"avgPrice", "2000"}},
            {{"symbol", "BTCUSDT"}, {"positionIdx", 1}, {"size", "0.01"}, {"avgPrice", "100"}},
            {{"symbol", "BTCUSDT"}, {"positionIdx", 2}, {"size", "0.02"}, {"avgPrice", "101"}}
        }}}}
    };

    const auto position = parse_hedge_positions(response, "BTCUSDT");

    CHECK(position.long_size == 0.01);
    CHECK(position.long_entry == 100.0);
    CHECK(position.short_size == 0.02);
    CHECK(position.short_entry == 101.0);
}

TEST_CASE("REST bootstrap rejects hedge rows whose side contradicts positionIdx")
{
    const nlohmann::json response = {
        {"retCode", 0},
        {"result", {{"list", {
            {{"symbol", "BTCUSDT"}, {"positionIdx", 1}, {"side", "Sell"}, {"size", "0.01"}},
            {{"symbol", "BTCUSDT"}, {"positionIdx", 2}, {"side", "Sell"}, {"size", "0"}}
        }}}}
    };

    CHECK_THROWS_WITH(parse_hedge_positions(response, "BTCUSDT"),
                      Catch::Matchers::ContainsSubstring("positionIdx 1 must be Buy"));
}

TEST_CASE("fee bootstrap requires the exact configured symbol entry")
{
    const nlohmann::json response = {
        {"retCode", 0},
        {"result", {{"list", {
            {{"symbol", "ETHUSDT"}, {"makerFeeRate", "0.001"}, {"takerFeeRate", "0.002"}},
            {{"symbol", "BTCUSDT"}, {"makerFeeRate", "0.0002"}, {"takerFeeRate", "0.00055"}}
        }}}}
    };

    const auto fees = parse_fee_rates(response, "BTCUSDT");

    CHECK(fees.maker == 0.0002);
    CHECK(fees.taker == 0.00055);
    CHECK_THROWS_WITH(parse_fee_rates(response, "SOLUSDT"),
                      Catch::Matchers::ContainsSubstring("fee entry missing for SOLUSDT"));
}

TEST_CASE("open-order bootstrap exposes exact-symbol resting notional")
{
    const nlohmann::json response = {
        {"retCode", 0},
        {"result", {{"list", {
            {{"symbol", "ETHUSDT"}, {"orderLinkId", "other"}, {"qty", "9"}, {"price", "2000"},
             {"reduceOnly", false}, {"orderStatus", "New"}},
            {{"symbol", "BTCUSDT"}, {"orderLinkId", "bid"}, {"qty", "0.02"}, {"price", "100"},
             {"reduceOnly", false}, {"orderStatus", "New"}},
            {{"symbol", "BTCUSDT"}, {"orderLinkId", "tp"}, {"qty", "0.01"}, {"price", "101"},
             {"reduceOnly", true}, {"orderStatus", "PartiallyFilled"}},
            {{"symbol", "BTCUSDT"}, {"orderLinkId", "old"}, {"qty", "1"}, {"price", "90"},
             {"reduceOnly", false}, {"orderStatus", "Cancelled"}}
        }}}}
    };

    const auto resting = parse_resting_orders(response, "BTCUSDT");

    REQUIRE(resting.size() == 2);
    CHECK(resting[0].name == "bid");
    CHECK(resting[0].qty == 0.02);
    CHECK_FALSE(resting[0].reduce_only);
    CHECK(resting[1].reduce_only);
}

TEST_CASE("open-order truth preserves fields needed for owned amend reconciliation")
{
    const nlohmann::json response = {
        {"retCode", 0},
        {"result", {{"list", {
            {{"symbol", "BTCUSDT"}, {"orderLinkId", "mm_BTC_123_bid_1_1"},
             {"side", "Buy"}, {"orderType", "Limit"}, {"qty", "0.02"},
             {"leavesQty", "0.01"}, {"price", "99"}, {"positionIdx", 1},
             {"reduceOnly", false}, {"timeInForce", "PostOnly"},
             {"orderStatus", "PartiallyFilled"}}
        }}}}
    };

    const auto orders = parse_open_orders(response, "BTCUSDT");

    REQUIRE(orders.size() == 1);
    CHECK(orders[0].order_link_id == "mm_BTC_123_bid_1_1");
    CHECK(orders[0].qty == 0.01);
    CHECK(orders[0].position_idx == 1);
    CHECK(orders[0].status == "PartiallyFilled");
}

TEST_CASE("exchange order classification cancels stale and duplicate owned names deterministically")
{
    const std::vector<OpenOrderView> orders{
        open_order("mm_BTC_123_bid_1_1", "Buy", 0.01, 99.0, 1),
        open_order("mm_BTC_123_bid_1_2", "Buy", 0.01, 99.0, 1),
        open_order("mm_BTC_999_ask_1_1", "Sell", 0.01, 101.0, 2),
        open_order("manual", "Buy", 0.02, 98.0, 1)};

    const auto classified = classify_open_orders(orders, "mm_BTC_", "mm_BTC_123_");

    REQUIRE(classified.cancel_links.size() == 3);
    CHECK(classified.cancel_links[0] == "mm_BTC_123_bid_1_1");
    CHECK(classified.cancel_links[1] == "mm_BTC_123_bid_1_2");
    CHECK(classified.cancel_links[2] == "mm_BTC_999_ask_1_1");
    CHECK(classified.working.empty());
    REQUIRE(classified.external.size() == 1);
    CHECK(classified.external[0].name == "external:manual");
}

TEST_CASE("legacy bot links are stale-owned while near misses remain external")
{
    const std::vector<std::string> legacy{
        "bid_mm_1720000000000_1", "ask_mm_1720000000000_2",
        "tp_sell_mm_1720000000000_3", "tp_buy_mm_1720000000000_4",
        "sl_long_mm_1720000000000_5", "sl_short_mm_1720000000000_6",
        "bid_mmlo_1720000000000_7", "tp_sell_mmlo_1720000000000_8",
        "sl_long_mmlo_1720000000000_9"};
    std::vector<OpenOrderView> orders;
    for (const auto &link : legacy)
        orders.push_back(open_order(link, "Buy", 0.01, 99.0, 1));
    orders.push_back(open_order("manual_mm_1720000000000_1", "Buy", 0.01, 99.0, 1));
    orders.push_back(open_order("ask_mmlo_1720000000000_1", "Sell", 0.01, 101.0, 2));
    orders.push_back(open_order("bid_mm_bad_1", "Buy", 0.01, 99.0, 1));
    orders.push_back(open_order("bid_mm_1720000000000_1_extra", "Buy", 0.01, 99.0, 1));

    const auto classified = classify_open_orders(orders, "mm_BTC_", "mm_BTC_123_");
    auto expected = legacy;
    std::sort(expected.begin(), expected.end());

    CHECK(classified.cancel_links == expected);
    CHECK(classified.working.empty());
    REQUIRE(classified.external.size() == 4);
}

TEST_CASE("external link IDs are namespaced away from owned strategy roles")
{
    const auto classified = classify_open_orders(
        {open_order("bid_1", "Buy", 0.08, 99.0, 1)}, "mm_BTC_", "mm_BTC_123_");

    CHECK(classified.cancel_links.empty());
    REQUIRE(classified.external.size() == 1);
    CHECK(classified.external[0].name == "external:bid_1");
}

TEST_CASE("instrument bootstrap requires complete positive linear lot and price bounds")
{
    const nlohmann::json response = {
        {"retCode", 0},
        {"result", {{"list", {{{"symbol", "BTCUSDT"},
                                 {"priceFilter", {{"tickSize", "0.1"}, {"minPrice", "0.5"},
                                                  {"maxPrice", "1000000"}}},
                                 {"lotSizeFilter", {{"qtyStep", "0.001"}, {"minOrderQty", "0.001"},
                                                    {"minNotionalValue", "5"}, {"maxOrderQty", "100"},
                                                    {"maxMktOrderQty", "50"}}}}}}}}};

    const auto meta = parse_instrument_meta(response, "BTCUSDT");

    CHECK(meta.tick_size == 0.1);
    CHECK(meta.lot_size == 0.001);
    CHECK(meta.min_notional == 5.0);
    CHECK(meta.max_limit_qty == 100.0);
    CHECK(meta.max_market_qty == 50.0);

    auto incomplete = response;
    incomplete["result"]["list"][0]["lotSizeFilter"].erase("minNotionalValue");
    CHECK_THROWS(parse_instrument_meta(incomplete, "BTCUSDT"));
}

TEST_CASE("owned order classification accepts base36 counters above nine")
{
    const std::vector<OpenOrderView> orders{
        open_order("mm_BTC_123_bid_1_a", "Buy", 0.01, 99.0, 1)};

    const auto classified = classify_open_orders(orders, "mm_BTC_", "mm_BTC_123_");

    CHECK(classified.cancel_links.empty());
    REQUIRE(classified.working.size() == 1);
    CHECK(classified.working[0].name == "bid_1");
}

TEST_CASE("REST account truth rejects non-finite and partially parsed numeric fields")
{
    const nlohmann::json positions = {
        {"result", {{"list", {
            {{"symbol", "BTCUSDT"}, {"positionIdx", 1}, {"size", "nan"}, {"avgPrice", "100"}},
            {{"symbol", "BTCUSDT"}, {"positionIdx", 2}, {"size", "0"}, {"avgPrice", ""}}
        }}}}
    };
    const nlohmann::json orders = {
        {"result", {{"list", {
            {{"symbol", "BTCUSDT"}, {"orderLinkId", "bad"}, {"orderStatus", "New"},
             {"qty", "0.01junk"}, {"price", "100"}}
        }}}}
    };

    CHECK_THROWS(parse_hedge_positions(positions, "BTCUSDT"));
    CHECK_THROWS(parse_open_orders(orders, "BTCUSDT"));
}

TEST_CASE("REST position truth requires explicit size and entry-price fields")
{
    const nlohmann::json valid = {
        {"result", {{"list", {
            {{"symbol", "BTCUSDT"}, {"positionIdx", 1}, {"side", "Buy"},
             {"size", "0.01"}, {"avgPrice", "100"}},
            {{"symbol", "BTCUSDT"}, {"positionIdx", 2}, {"side", "Sell"},
             {"size", "0"}, {"avgPrice", ""}}
        }}}}
    };
    auto missing_size = valid;
    missing_size["result"]["list"][0].erase("size");
    auto missing_entry = valid;
    missing_entry["result"]["list"][0].erase("avgPrice");
    auto missing_flat_entry = valid;
    missing_flat_entry["result"]["list"][1].erase("avgPrice");

    CHECK_NOTHROW(parse_hedge_positions(valid, "BTCUSDT"));
    CHECK_THROWS(parse_hedge_positions(missing_size, "BTCUSDT"));
    CHECK_THROWS(parse_hedge_positions(missing_entry, "BTCUSDT"));
    CHECK_THROWS(parse_hedge_positions(missing_flat_entry, "BTCUSDT"));
}

TEST_CASE("REST open-order truth rejects exact-symbol rows without a status")
{
    const nlohmann::json response = {
        {"result", {{"list", {
            {{"symbol", "BTCUSDT"}, {"orderLinkId", "hidden"},
             {"qty", "0.01"}, {"price", "100"}}
        }}}}
    };

    CHECK_THROWS(parse_open_orders(response, "BTCUSDT"));
}
