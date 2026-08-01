#include <catch2/catch_test_macros.hpp>

#include "market_state.hpp"

TEST_CASE("orderbook deltas update insert and delete price levels")
{
    MarketState state;
    CHECK(state.generation() == 0);
    const auto now = std::chrono::steady_clock::time_point{std::chrono::seconds{10}};
    REQUIRE(state.apply({{"topic", "orderbook.50.BTCUSDT"},
                         {"type", "snapshot"},
                         {"data", {{"s", "BTCUSDT"},
                                   {"u", 10},
                                   {"seq", 100},
                                   {"b", nlohmann::json::array({{"100", "1"}, {"99", "2"}})},
                                   {"a", nlohmann::json::array({{"101", "1"}, {"102", "2"}})}}}},
                        now));
    CHECK(state.generation() == 1);
    REQUIRE(state.apply({{"topic", "orderbook.50.BTCUSDT"},
                         {"type", "delta"},
                         {"data", {{"s", "BTCUSDT"},
                                   {"u", 11},
                                   {"seq", 101},
                                   {"b", nlohmann::json::array({{"100", "0"}, {"98", "3"}})},
                                   {"a", nlohmann::json::array({{"101", "4"}})}}}},
                        now + std::chrono::milliseconds{10}));
    CHECK(state.generation() == 2);

    const auto book = state.orderbook("BTCUSDT");
    REQUIRE(book.has_value());
    CHECK((*book)["b"] == nlohmann::json::array({{"99", "2"}, {"98", "3"}}));
    CHECK((*book)["a"] == nlohmann::json::array({{"101", "4"}, {"102", "2"}}));
}

TEST_CASE("readiness requires connected fresh same-symbol ticker and sequence-valid book")
{
    MarketState state;
    const auto now = std::chrono::steady_clock::time_point{std::chrono::seconds{20}};
    state.set_connected(true);
    CHECK_FALSE(state.apply({{"topic", "tickers.BTCUSDT"},
                             {"type", "snapshot"},
                             {"data", {{"symbol", "BTCUSDT"}, {"lastPrice", "100"}}}},
                            now));
    REQUIRE(state.apply({{"topic", "orderbook.1.BTCUSDT"},
                         {"type", "snapshot"},
                         {"data", {{"s", "BTCUSDT"},
                                   {"u", 20},
                                   {"seq", 200},
                                   {"b", nlohmann::json::array({{"99", "1"}})},
                                   {"a", nlohmann::json::array({{"101", "1"}})}}}},
                        now));

    CHECK_FALSE(state.ready("BTCUSDT", now + std::chrono::seconds{4}, std::chrono::seconds{5}));
    REQUIRE(state.apply({{"topic", "tickers.BTCUSDT"},
                         {"type", "delta"},
                         {"data", {{"symbol", "BTCUSDT"}, {"markPrice", "100.1"}}}},
                        now + std::chrono::milliseconds{1}));
    CHECK(state.ready("BTCUSDT", now + std::chrono::milliseconds{500}, std::chrono::seconds{5}));
    CHECK_FALSE(state.ready("ETHUSDT", now + std::chrono::seconds{4}, std::chrono::seconds{5}));
    CHECK_FALSE(state.ready("BTCUSDT", now + std::chrono::seconds{6}, std::chrono::seconds{5}));
    state.set_connected(false);
    CHECK_FALSE(state.ready("BTCUSDT", now, std::chrono::seconds{5}));
    state.set_connected(true);
    CHECK_FALSE(state.apply({{"topic", "tickers.BTCUSDT"},
                             {"type", "delta"},
                             {"data", {{"symbol", "BTCUSDT"}, {"lastPrice", "101"}}}},
                            now + std::chrono::milliseconds{1}));
    CHECK_FALSE(state.ready("BTCUSDT", now, std::chrono::seconds{5}));
}

TEST_CASE("malformed market payloads and negative sequence values fail closed")
{
    MarketState state;
    const auto now = MarketState::Clock::time_point{};

    CHECK_FALSE(state.apply({{"topic", "tickers.BTCUSDT"}, {"data", "not-an-object"}}, now));
    CHECK_FALSE(state.apply({{"topic", "orderbook.1.BTCUSDT"},
                             {"type", "snapshot"},
                             {"data", {{"s", "BTCUSDT"}, {"u", -1}, {"seq", -1},
                                       {"b", nlohmann::json::array({{"99", "1"}})},
                                       {"a", nlohmann::json::array({{"101", "1"}})}}}},
                            now));
    CHECK_FALSE(state.apply({{"topic", "orderbook.1.BTCUSDT"},
                             {"type", "snapshot"},
                             {"data", {{"s", "BTCUSDT"}, {"u", 0}, {"seq", 0},
                                       {"b", nlohmann::json::array({{"99", "1"}})},
                                       {"a", nlohmann::json::array({{"101", "1"}})}}}},
                            now));
    CHECK_FALSE(state.apply({{"topic", "orderbook.1.BTCUSDT"},
                             {"type", "snapshot"},
                             {"data", {{"s", "BTCUSDT"}, {"u", 1}, {"seq", 1},
                                       {"b", nlohmann::json::array({{"99junk", "1"}})},
                                       {"a", nlohmann::json::array({{"101", "1"}})}}}},
                            now));
    CHECK(state.generation() == 0);
}

TEST_CASE("malformed orderbook updates invalidate an initialized book immediately")
{
    MarketState state;
    const auto now = MarketState::Clock::time_point{std::chrono::seconds{30}};
    state.set_connected(true);
    REQUIRE(state.apply({{"topic", "tickers.BTCUSDT"}, {"type", "snapshot"},
                         {"data", {{"symbol", "BTCUSDT"}, {"markPrice", "100"}}}}, now));
    REQUIRE(state.apply({{"topic", "orderbook.1.BTCUSDT"}, {"type", "snapshot"},
                         {"data", {{"s", "BTCUSDT"}, {"u", 1}, {"seq", 1},
                                   {"b", nlohmann::json::array({{"99", "1"}})},
                                   {"a", nlohmann::json::array({{"101", "1"}})}}}}, now));
    REQUIRE(state.ready("BTCUSDT", now, std::chrono::seconds{5}));
    const auto generation = state.generation();

    state.apply({{"topic", "orderbook.1.BTCUSDT"}, {"type", "delta"},
                 {"data", {{"s", "BTCUSDT"}, {"u", 2},
                           {"b", nlohmann::json::array({{"99", "nan"}})},
                           {"a", nlohmann::json::array()}}}}, now + std::chrono::milliseconds{1});

    CHECK_FALSE(state.ready("BTCUSDT", now + std::chrono::milliseconds{1}, std::chrono::seconds{5}));
    CHECK(state.generation() > generation);
}

TEST_CASE("unrelated ticker deltas cannot refresh a retained mark price")
{
    MarketState state;
    const auto now = MarketState::Clock::time_point{std::chrono::seconds{40}};
    state.set_connected(true);
    REQUIRE(state.apply({{"topic", "tickers.BTCUSDT"}, {"type", "snapshot"},
                         {"data", {{"symbol", "BTCUSDT"}, {"markPrice", "100"}}}}, now));
    REQUIRE(state.apply({{"topic", "orderbook.1.BTCUSDT"}, {"type", "snapshot"},
                         {"data", {{"s", "BTCUSDT"}, {"u", 1}, {"seq", 1},
                                   {"b", nlohmann::json::array({{"99", "1"}})},
                                   {"a", nlohmann::json::array({{"101", "1"}})}}}}, now));
    REQUIRE(state.apply({{"topic", "orderbook.1.BTCUSDT"}, {"type", "delta"},
                         {"data", {{"s", "BTCUSDT"}, {"u", 2}, {"seq", 2},
                                   {"b", nlohmann::json::array()},
                                   {"a", nlohmann::json::array()}}}}, now + std::chrono::seconds{4}));
    REQUIRE(state.apply({{"topic", "tickers.BTCUSDT"}, {"type", "delta"},
                         {"data", {{"symbol", "BTCUSDT"}, {"lastPrice", "101"}}}},
                        now + std::chrono::seconds{4}));

    CHECK_FALSE(state.ready("BTCUSDT", now + std::chrono::seconds{6}, std::chrono::seconds{5}));
}

TEST_CASE("payload symbols cannot redirect topic state")
{
    MarketState state;
    const auto now = MarketState::Clock::time_point{std::chrono::seconds{50}};
    state.set_connected(true);
    REQUIRE(state.apply({{"topic", "tickers.BTCUSDT"}, {"type", "snapshot"},
                         {"data", {{"symbol", "BTCUSDT"}, {"markPrice", "100"}}}}, now));
    REQUIRE(state.apply({{"topic", "orderbook.1.BTCUSDT"}, {"type", "snapshot"},
                         {"data", {{"s", "BTCUSDT"}, {"u", 1}, {"seq", 1},
                                   {"b", nlohmann::json::array({{"99", "1"}})},
                                   {"a", nlohmann::json::array({{"101", "1"}})}}}}, now));
    REQUIRE(state.ready("BTCUSDT", now, std::chrono::seconds{5}));

    state.apply({{"topic", "orderbook.1.BTCUSDT"}, {"type", "delta"},
                 {"data", {{"s", "ETHUSDT"}, {"u", 2}, {"seq", 2},
                           {"b", nlohmann::json::array()}, {"a", nlohmann::json::array()}}}}, now);
    CHECK_FALSE(state.ready("BTCUSDT", now, std::chrono::seconds{5}));

    REQUIRE(state.apply({{"topic", "orderbook.1.BTCUSDT"}, {"type", "snapshot"},
                         {"data", {{"s", "BTCUSDT"}, {"u", 3}, {"seq", 3},
                                   {"b", nlohmann::json::array({{"99", "1"}})},
                                   {"a", nlohmann::json::array({{"101", "1"}})}}}}, now));
    state.apply({{"topic", "tickers.BTCUSDT"}, {"type", "delta"},
                 {"data", {{"symbol", "ETHUSDT"}, {"markPrice", "2000"}}}}, now);
    CHECK_FALSE(state.ready("BTCUSDT", now, std::chrono::seconds{5}));
}

TEST_CASE("malformed ticker mark invalidates readiness immediately")
{
    MarketState state;
    const auto now = MarketState::Clock::time_point{std::chrono::seconds{60}};
    state.set_connected(true);
    REQUIRE(state.apply({{"topic", "tickers.BTCUSDT"}, {"type", "snapshot"},
                         {"data", {{"symbol", "BTCUSDT"}, {"markPrice", "100"}}}}, now));
    REQUIRE(state.apply({{"topic", "orderbook.1.BTCUSDT"}, {"type", "snapshot"},
                         {"data", {{"s", "BTCUSDT"}, {"u", 1}, {"seq", 1},
                                   {"b", nlohmann::json::array({{"99", "1"}})},
                                   {"a", nlohmann::json::array({{"101", "1"}})}}}}, now));

    state.apply({{"topic", "tickers.BTCUSDT"}, {"type", "delta"},
                 {"data", {{"symbol", "BTCUSDT"}, {"markPrice", "nan"}}}}, now);

    CHECK_FALSE(state.ready("BTCUSDT", now, std::chrono::seconds{5}));
}

TEST_CASE("mark price freshness is stricter than orderbook freshness")
{
    MarketState state;
    const auto now = MarketState::Clock::time_point{std::chrono::seconds{70}};
    state.set_connected(true);
    REQUIRE(state.apply({{"topic", "tickers.BTCUSDT"}, {"type", "snapshot"},
                         {"data", {{"symbol", "BTCUSDT"}, {"markPrice", "100"}}}}, now));
    REQUIRE(state.apply({{"topic", "orderbook.1.BTCUSDT"}, {"type", "snapshot"},
                         {"data", {{"s", "BTCUSDT"}, {"u", 1}, {"seq", 1},
                                   {"b", nlohmann::json::array({{"99", "1"}})},
                                   {"a", nlohmann::json::array({{"101", "1"}})}}}}, now));
    REQUIRE(state.apply({{"topic", "orderbook.1.BTCUSDT"}, {"type", "delta"},
                         {"data", {{"s", "BTCUSDT"}, {"u", 2}, {"seq", 2},
                                   {"b", nlohmann::json::array()},
                                   {"a", nlohmann::json::array()}}}}, now + std::chrono::milliseconds{900}));

    CHECK(state.ready("BTCUSDT", now + std::chrono::milliseconds{999},
                      std::chrono::seconds{5}, std::chrono::seconds{1}));
    CHECK_FALSE(state.ready("BTCUSDT", now + std::chrono::milliseconds{1001},
                            std::chrono::seconds{5}, std::chrono::seconds{1}));
}

TEST_CASE("market message type is explicit while a valid u-one delta resets the book")
{
    MarketState state;
    const auto now = MarketState::Clock::time_point{std::chrono::seconds{80}};
    state.set_connected(true);
    REQUIRE(state.apply({{"topic", "tickers.BTCUSDT"}, {"type", "snapshot"},
                         {"data", {{"symbol", "BTCUSDT"}, {"markPrice", "100"}}}}, now));
    REQUIRE(state.apply({{"topic", "orderbook.1.BTCUSDT"}, {"type", "snapshot"},
                         {"data", {{"s", "BTCUSDT"}, {"u", 2}, {"seq", 2},
                                   {"b", nlohmann::json::array({{"99", "1"}, {"98", "1"}})},
                                   {"a", nlohmann::json::array({{"101", "1"}})}}}}, now));

    CHECK_FALSE(state.apply({{"topic", "orderbook.1.BTCUSDT"},
                             {"data", {{"s", "BTCUSDT"}, {"u", 3}, {"seq", 3},
                                       {"b", nlohmann::json::array({{"97", "1"}})},
                                       {"a", nlohmann::json::array({{"102", "1"}})}}}}, now));
    CHECK_FALSE(state.orderbook("BTCUSDT").has_value());
    CHECK_FALSE(state.apply({{"topic", "tickers.BTCUSDT"},
                             {"data", {{"symbol", "BTCUSDT"}, {"markPrice", "101"}}}}, now));

    REQUIRE(state.apply({{"topic", "orderbook.1.BTCUSDT"}, {"type", "snapshot"},
                         {"data", {{"s", "BTCUSDT"}, {"u", 4}, {"seq", 4},
                                   {"b", nlohmann::json::array({{"99", "1"}})},
                                   {"a", nlohmann::json::array({{"101", "1"}})}}}}, now));
    bool accepted = true;
    CHECK_NOTHROW(accepted = state.apply(
                      {{"topic", "orderbook.1.BTCUSDT"}, {"type", 7},
                       {"data", {{"s", "BTCUSDT"}, {"u", 5}, {"seq", 5},
                                 {"b", nlohmann::json::array()},
                                 {"a", nlohmann::json::array()}}}}, now));
    CHECK_FALSE(accepted);

    REQUIRE(state.apply({{"topic", "orderbook.1.BTCUSDT"}, {"type", "delta"},
                         {"data", {{"s", "BTCUSDT"}, {"u", 1}, {"seq", 6},
                                   {"b", nlohmann::json::array({{"97", "1"}})},
                                   {"a", nlohmann::json::array({{"103", "1"}})}}}}, now));
    REQUIRE(state.orderbook("BTCUSDT").has_value());
    CHECK((*state.orderbook("BTCUSDT"))["b"] == nlohmann::json::array({{"97", "1"}}));
}

TEST_CASE("snapshot levels require positive quantity while deltas may delete")
{
    MarketState state;
    const auto now = MarketState::Clock::time_point{std::chrono::seconds{90}};
    state.set_connected(true);
    REQUIRE(state.apply({{"topic", "tickers.BTCUSDT"}, {"type", "snapshot"},
                         {"data", {{"symbol", "BTCUSDT"}, {"markPrice", "100"}}}}, now));
    CHECK_FALSE(state.apply({{"topic", "orderbook.1.BTCUSDT"}, {"type", "snapshot"},
                             {"data", {{"s", "BTCUSDT"}, {"u", 2}, {"seq", 2},
                                       {"b", nlohmann::json::array({{"99", "0"}})},
                                       {"a", nlohmann::json::array({{"101", "1"}})}}}}, now));
    CHECK_FALSE(state.ready("BTCUSDT", now, std::chrono::seconds{5}));

    REQUIRE(state.apply({{"topic", "orderbook.1.BTCUSDT"}, {"type", "snapshot"},
                         {"data", {{"s", "BTCUSDT"}, {"u", 3}, {"seq", 3},
                                   {"b", nlohmann::json::array({{"99", "1"}, {"98", "1"}})},
                                   {"a", nlohmann::json::array({{"101", "1"}})}}}}, now));
    REQUIRE(state.apply({{"topic", "orderbook.1.BTCUSDT"}, {"type", "delta"},
                         {"data", {{"s", "BTCUSDT"}, {"u", 4}, {"seq", 4},
                                   {"b", nlohmann::json::array({{"99", "0"}})},
                                   {"a", nlohmann::json::array()}}}}, now));
    CHECK((*state.orderbook("BTCUSDT"))["b"] == nlohmann::json::array({{"98", "1"}}));
    CHECK_FALSE(state.apply({{"topic", "orderbook.1.BTCUSDT"}, {"type", "delta"},
                             {"data", {{"s", "BTCUSDT"}, {"u", 1}, {"seq", 5},
                                       {"b", nlohmann::json::array({{"97", "0"}})},
                                       {"a", nlohmann::json::array({{"103", "1"}})}}}}, now));
}

TEST_CASE("a malformed raw public frame disconnects and clears prior ready state")
{
    MarketState state;
    const auto now = MarketState::Clock::time_point{std::chrono::seconds{100}};
    state.set_connected(true);
    REQUIRE(state.apply({{"topic", "tickers.BTCUSDT"}, {"type", "snapshot"},
                         {"data", {{"symbol", "BTCUSDT"}, {"markPrice", "100"}}}}, now));
    REQUIRE(state.apply({{"topic", "orderbook.1.BTCUSDT"}, {"type", "snapshot"},
                         {"data", {{"s", "BTCUSDT"}, {"u", 1}, {"seq", 1},
                                   {"b", nlohmann::json::array({{"99", "1"}})},
                                   {"a", nlohmann::json::array({{"101", "1"}})}}}}, now));
    REQUIRE(state.ready("BTCUSDT", now, std::chrono::seconds{5}));

    CHECK_THROWS(state.apply_frame("{not-json", now));

    CHECK_FALSE(state.ready("BTCUSDT", now, std::chrono::seconds{5}));
    CHECK_FALSE(state.ticker("BTCUSDT").has_value());
    CHECK_FALSE(state.orderbook("BTCUSDT").has_value());
}
