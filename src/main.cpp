#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "market_data_feed.hpp"
#include "strategy.hpp"
#include "trading_helper.hpp"

std::string get_env(const char *name, const std::string &fallback = "")
{
    const char *v = std::getenv(name);
    return v ? std::string{v} : fallback;
}

std::optional<InstrumentMeta> parse_instrument_meta(const nlohmann::json &instruments, const std::string &symbol)
{
    if (!instruments.contains("result") || !instruments["result"].contains("list"))
        return std::nullopt;
    for (const auto &item : instruments["result"]["list"])
    {
        if (!item.contains("symbol"))
            continue;
        if (item["symbol"].get<std::string>() != symbol)
            continue;
        InstrumentMeta meta{};
        try
        {
            if (item.contains("priceFilter") && item["priceFilter"].contains("tickSize"))
            {
                meta.tick_size = std::stod(item["priceFilter"]["tickSize"].get<std::string>());
            }
            if (item.contains("lotSizeFilter"))
            {
                const auto &lf = item["lotSizeFilter"];
                if (lf.contains("qtyStep"))
                    meta.lot_size = std::stod(lf["qtyStep"].get<std::string>());
                if (lf.contains("minQty"))
                    meta.min_qty = std::stod(lf["minQty"].get<std::string>());
                else if (lf.contains("minOrderQty"))
                    meta.min_qty = std::stod(lf["minOrderQty"].get<std::string>());
                else if (lf.contains("minTradeNum"))
                    meta.min_qty = std::stod(lf["minTradeNum"].get<std::string>());
            }
        }
        catch (...)
        {
            return std::nullopt;
        }
        return meta;
    }
    return std::nullopt;
}

std::vector<std::string> list_symbols(const nlohmann::json &instruments, size_t limit = 10)
{
    std::vector<std::string> out;
    if (!instruments.contains("result") || !instruments["result"].contains("list"))
        return out;
    for (const auto &item : instruments["result"]["list"])
    {
        if (item.contains("symbol"))
        {
            out.push_back(item["symbol"].get<std::string>());
            if (out.size() >= limit)
                break;
        }
    }
    return out;
}

int main(int argc, char **argv)
{
    const std::string symbol = (argc > 1) ? argv[1] : get_env("BYBIT_SYMBOL", "BTCUSDT");
    const std::string api_key = get_env("BYBIT_API_KEY");
    const std::string api_secret = get_env("BYBIT_API_SECRET");
    const std::string base_url = get_env("BYBIT_BASE_URL", "https://api.bybit.com");
    // Use linear for market data and order placement; wallet uses unified explicitly.
    const std::string trade_category = "linear";
    const std::string wallet_category = "UNIFIED";
    const std::string ws_url = get_env("BYBIT_WS_PUBLIC_URL", "wss://stream.bybit.com/v5/public/linear");
    const bool run_live = get_env("BYBIT_RUN_LIVE", "0") == "1";
    const double budget_usd = std::stod(get_env("BYBIT_BUDGET_USD", "10.0"));
    const double min_spread_bps = std::stod(get_env("BYBIT_MIN_SPREAD_BPS", "1.0"));
    const double spread_factor = std::stod(get_env("BYBIT_SPREAD_FACTOR", "1.0"));
    const int ticks = std::stoi(get_env("BYBIT_TICKS", "5")); // set <=0 to run until interrupted
    const int tick_delay_sec = std::stoi(get_env("BYBIT_TICK_DELAY_SEC", "3"));

    try
    {
        TradingHelper helper(api_key, api_secret, trade_category, base_url);

        // Instrument metadata for sizing/rounding (always query market category linear for perp instruments)
        auto instruments = helper.fetch_instruments_info_for_category("linear");
        std::cout << "[debug] instruments_info fetched\n";
        auto meta_opt = parse_instrument_meta(instruments, symbol);
        if (!meta_opt.has_value() || meta_opt->tick_size <= 0 || meta_opt->lot_size <= 0 || meta_opt->min_qty <= 0)
        {
            auto syms = list_symbols(instruments);
            std::cerr << "Failed to load instrument meta for " << symbol << " (tick/lot/min missing)."
                      << " Available symbols sample: ";
            for (const auto &s : syms)
                std::cerr << s << " ";
            // Print the entry for this symbol if present.
            if (instruments.contains("result") && instruments["result"].contains("list"))
            {
                for (const auto &item : instruments["result"]["list"])
                {
                    if (item.contains("symbol") && item["symbol"].get<std::string>() == symbol)
                    {
                        std::cerr << "\nInstrument entry for " << symbol << ": " << item.dump() << std::endl;
                        break;
                    }
                }
            }
            std::cerr << "\nRaw instruments response (truncated): " << instruments.dump().substr(0, 2000) << std::endl;
            return 1;
        }
        auto meta = *meta_opt;

        if (helper.has_credentials())
        {
            try
            {
                auto wallet = helper.fetch_wallet_balance_for_category(wallet_category, std::nullopt);
                std::cout << "Wallet: " << wallet.dump() << std::endl;
            }
            catch (const std::exception &ex)
            {
                std::cerr << "Wallet fetch failed: " << ex.what() << std::endl;
            }
        }
        else
        {
            std::cout << "No API keys set; running read-only." << std::endl;
        }

        MarketDataFeed feed(ws_url);
        feed.start({symbol}, 1);
        if (!feed.wait_for_initial(std::chrono::milliseconds{5000}))
        {
            std::cerr << "Timed out waiting for initial market data" << std::endl;
            return 1;
        }

        ExampleMarketMakerStrategy strategy(symbol, meta, budget_usd, min_spread_bps, spread_factor);

        int i = 0;
        do
        {
            auto ob = feed.latest_orderbook(symbol);
            auto tk = feed.latest_ticker(symbol);
            if (!ob || !tk)
            {
                std::cerr << "Missing data on tick " << i << std::endl;
                break;
            }
            MarketDataSnapshot snap{symbol, *tk, *ob};
            strategy.on_snapshot(snap, helper, run_live && helper.has_credentials());
            ++i;
            std::this_thread::sleep_for(std::chrono::seconds{tick_delay_sec});
        } while (ticks <= 0 || i < ticks);

        // Cleanup
        if (run_live && helper.has_credentials())
        {
            helper.cancel_all(symbol);
        }

        feed.stop();
        std::cout << "Done." << std::endl;
    }
    catch (const std::exception &ex)
    {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }
    return 0;
}
