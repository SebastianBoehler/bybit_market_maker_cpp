#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>
#include <vector>

#include "market_data_feed.hpp"

namespace
{
    std::string get_env(const char *name, const std::string &fallback = "")
    {
        const char *v = std::getenv(name);
        return v ? std::string{v} : fallback;
    }
}

TEST_CASE("ws_feed_initial_data", "[smoke][network][ws]")
{
    const std::string symbol = get_env("BYBIT_SYMBOL", "BTCUSDT");
    const std::string ws_url = get_env("BYBIT_WS_PUBLIC_URL", "wss://stream.bybit.com/v5/public/linear");

    MarketDataFeed feed(ws_url);
    feed.start({symbol}, 1);

    const bool ok = feed.wait_for_initial(std::chrono::milliseconds{10000});
    feed.stop();

    REQUIRE(ok);
    auto tk = feed.latest_ticker(symbol);
    auto ob = feed.latest_orderbook(symbol);
    REQUIRE(tk.has_value());
    REQUIRE(ob.has_value());
}
