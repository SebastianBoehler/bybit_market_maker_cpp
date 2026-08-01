#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <limits>
#include <cstdlib>

#include "app_config.hpp"

TEST_CASE("requested live trading rejects missing credentials and unbounded risk")
{
    AppConfig config;
    config.run_live = true;
    config.symbol = "BTCUSDT";
    config.budget_usd = 10.0;
    config.max_net_qty = 1.0;
    config.gross_notional_cap = 100.0;
    config.ladder_levels = 1;

    CHECK_THROWS_WITH(validate_config(config),
                      Catch::Matchers::ContainsSubstring("API key/secret"));
    config.api_key = "key";
    config.api_secret = "secret";
    config.gross_notional_cap = -1.0;
    CHECK_THROWS_WITH(validate_config(config),
                      Catch::Matchers::ContainsSubstring("gross notional cap"));
}

TEST_CASE("environment numeric values require full-string parsing")
{
    setenv("BYBIT_RUN_LIVE", "0", 1);
    setenv("BYBIT_BUDGET_USD", "10junk", 1);
    char executable[] = "market-maker";
    char *arguments[] = {executable};

    CHECK_THROWS(load_config(1, arguments, "both"));
}

TEST_CASE("live config requires positive sizing and known side mode")
{
    AppConfig config;
    config.run_live = true;
    config.symbol = "BTCUSDT";
    config.api_key = "key";
    config.api_secret = "secret";
    config.budget_usd = 10.0;
    config.max_net_qty = 1.0;
    config.gross_notional_cap = 100.0;
    config.ladder_levels = 1;
    config.side_mode = "invalid";

    CHECK_THROWS_WITH(validate_config(config),
                      Catch::Matchers::ContainsSubstring("side mode"));
    config.side_mode = "both";
    CHECK_NOTHROW(validate_config(config));

    config.budget_usd = std::numeric_limits<double>::quiet_NaN();
    CHECK_THROWS(validate_config(config));
    config.budget_usd = 10.0;
    config.gross_notional_cap = std::numeric_limits<double>::infinity();
    CHECK_THROWS(validate_config(config));
    config.gross_notional_cap = 100.0;
    config.stop_loss_bps = 10000.0;
    CHECK_THROWS(validate_config(config));
    config.stop_loss_bps = -1.0;
    config.ladder_levels = 10;
    CHECK_THROWS(validate_config(config));
}

TEST_CASE("endpoint URLs cannot be empty at their point of use")
{
    AppConfig config;
    config.base_url.clear();
    CHECK_THROWS_WITH(validate_config(config),
                      Catch::Matchers::ContainsSubstring("base URL"));
    config.base_url = "https://api.bybit.com";
    config.public_ws_url.clear();
    CHECK_THROWS_WITH(validate_config(config),
                      Catch::Matchers::ContainsSubstring("public websocket URL"));

    config.public_ws_url = "wss://stream.bybit.com/v5/public/linear";
    config.private_ws_url.clear();
    CHECK_NOTHROW(validate_config(config));
    config.run_live = true;
    config.api_key = "key";
    config.api_secret = "secret";
    config.gross_notional_cap = 100.0;
    CHECK_THROWS_WITH(validate_config(config),
                      Catch::Matchers::ContainsSubstring("private websocket URL"));
}
