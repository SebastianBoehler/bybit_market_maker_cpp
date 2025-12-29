#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <string>

#include "trading_helper.hpp"

namespace
{
    std::string get_env(const char *name, const std::string &fallback = "")
    {
        const char *v = std::getenv(name);
        return v ? std::string{v} : fallback;
    }
}

TEST_CASE("fetch_snapshot_public_endpoints", "[smoke][network]")
{
    const std::string symbol = get_env("BYBIT_SYMBOL", "BTCUSDT");
    const std::string base_url = get_env("BYBIT_BASE_URL", "https://api.bybit.com");
    const std::string category = get_env("BYBIT_CATEGORY", "linear");

    TradingHelper helper("", "", category, base_url); // public endpoints only

    auto snap = helper.fetch_snapshot(symbol, 5);

    REQUIRE(snap.symbol == symbol);
    REQUIRE(snap.ticker.contains("result"));
    REQUIRE(snap.orderbook.contains("result"));
}
