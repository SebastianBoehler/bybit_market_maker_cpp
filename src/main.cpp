#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include <fstream>
#include <sstream>
#include <mutex>

#include <nlohmann/json.hpp>
#include <bybit/websocket_client.hpp>

#include "market_data_feed.hpp"
#include "pnl_tracker.hpp"
#include "strategy.hpp"
#include "trading_helper.hpp"

#ifndef DEFAULT_SIDE_MODE
#define DEFAULT_SIDE_MODE "both"
#endif

// ANSI color helpers for log readability.
constexpr const char *CLR_RESET = "\033[0m";
constexpr const char *CLR_CYAN = "\033[36m";
constexpr const char *CLR_YELLOW = "\033[33m";
constexpr const char *CLR_GREEN = "\033[32m";
constexpr const char *CLR_MAGENTA = "\033[35m";
constexpr const char *CLR_BLUE = "\033[34m";
constexpr const char *CLR_RED = "\033[31m";

inline std::string color_num(double v)
{
    if (v > 0)
        return std::string(CLR_GREEN) + std::to_string(v) + CLR_RESET;
    if (v < 0)
        return std::string(CLR_RED) + std::to_string(v) + CLR_RESET;
    return std::to_string(v);
}

std::string get_env(const char *name, const std::string &fallback = "")
{
    const char *v = std::getenv(name);
    return v ? std::string{v} : fallback;
}

std::unique_ptr<bybit::WebSocketClient> start_private_ws(const std::string &endpoint,
                                                         const std::string &api_key,
                                                         const std::string &api_secret,
                                                         PnlTracker &pnl_tracker,
                                                         PositionView &pos_view,
                                                         std::mutex &pos_mu)
{
    auto ws = std::make_unique<bybit::WebSocketClient>(endpoint, api_key, api_secret);
    ws->enable_auto_reconnect(true, 8);
    ws->set_message_handler([&pnl_tracker, &pos_view, &pos_mu](const std::string &msg)
                            {
        try
        {
            auto j = nlohmann::json::parse(msg);
            if (!j.contains("topic"))
                return;
            const std::string topic = j["topic"].get<std::string>();
            auto as_string = [](const nlohmann::json &v) -> std::string
            {
                if (v.is_string())
                    return v.get<std::string>();
                if (v.is_number())
                    return std::to_string(v.get<double>());
                return "";
            };
            auto as_double = [](const nlohmann::json &v) -> double
            {
                try
                {
                    if (v.is_string())
                        return std::stod(v.get<std::string>());
                    if (v.is_number())
                        return v.get<double>();
                }
                catch (...)
                {
                }
                return 0.0;
            };
            auto get_num_field = [&](const nlohmann::json &obj, const char *key) -> double
            {
                if (!obj.contains(key))
                    return 0.0;
                return as_double(obj.at(key));
            };
            auto get_str_field = [&](const nlohmann::json &obj, const char *key) -> std::string
            {
                if (!obj.contains(key))
                    return std::string{};
                return as_string(obj.at(key));
            };
            if (topic.find("execution") != std::string::npos && j.contains("data"))
            {
                for (const auto &d : j["data"])
                {
                    std::string link = get_str_field(d, "orderLinkId");
                    if (link.empty() && d.contains("orderId"))
                        link = get_str_field(d, "orderId");
                    double fee = get_num_field(d, "execFee");
                    double pnl = get_num_field(d, "execPnl");
                    if (pnl == 0.0 && d.contains("closedPnl"))
                        pnl = get_num_field(d, "closedPnl");
                    pnl_tracker.add_execution(link, pnl, fee);
                    std::cout << CLR_GREEN << "[EXE]" << CLR_RESET << " link=" << link << " qty=" << get_str_field(d, "execQty") << " price=" << get_str_field(d, "execPrice")
                              << " pnl=" << color_num(pnl) << " fee=" << fee << " side=" << get_str_field(d, "side") << "\n";
                    auto totals = pnl_tracker.totals();
                    double net = (totals.realized - totals.fees + totals.funding + totals.unrealized);
                    std::cout << CLR_MAGENTA << "[PNL]" << CLR_RESET << " realized=" << color_num(totals.realized) << " fees=" << totals.fees
                              << " funding=" << color_num(totals.funding) << " upl=" << color_num(totals.unrealized)
                              << " net=" << color_num(net) << "\n";
                }
            }
            else if (topic.find("position") != std::string::npos && j.contains("data"))
            {
                {
                    std::lock_guard<std::mutex> lg(pos_mu);
                    pos_view.long_size = 0.0;
                    pos_view.short_size = 0.0;
                    pos_view.long_entry = 0.0;
                    pos_view.short_entry = 0.0;
                }
                for (const auto &p : j["data"])
                {
                    const std::string sym = get_str_field(p, "symbol");
                    const std::string side = get_str_field(p, "side");
                    // Track unrealized PnL per symbol/side.
                    double upl_val = 0.0;
                    if (p.contains("unrealisedPnl"))
                        upl_val = as_double(p.at("unrealisedPnl"));
                    double funding_fee = get_num_field(p, "occFundingFee");
                    if (!sym.empty() && !side.empty())
                    {
                        pnl_tracker.set_unrealized(sym + "_" + side, upl_val);
                        if (funding_fee != 0.0)
                            pnl_tracker.add_funding(funding_fee);
                        std::lock_guard<std::mutex> lg(pos_mu);
                        if (side == "Buy")
                        {
                            pos_view.long_size = as_double(p.value("size", "0"));
                            pos_view.long_entry = as_double(p.value("avgPrice", "0"));
                        }
                        else if (side == "Sell")
                        {
                            pos_view.short_size = as_double(p.value("size", "0"));
                            pos_view.short_entry = as_double(p.value("avgPrice", "0"));
                        }
                    }
                    std::cout << CLR_YELLOW << "[POS]" << CLR_RESET << " sym=" << sym << " side=" << side << " size=" << get_str_field(p, "size")
                              << " entry=" << get_str_field(p, "avgPrice") << " upl=" << color_num(as_double(p.value("unrealisedPnl", "0")))
                              << " lev=" << get_str_field(p, "leverage") << " posIdx=" << get_str_field(p, "positionIdx")
                              << " fundingFee=" << funding_fee << " occClosingFee=" << get_num_field(p, "occClosingFee") << "\n";
                }
                auto totals = pnl_tracker.totals();
                double net = (totals.realized - totals.fees + totals.funding + totals.unrealized);
                std::cout << CLR_MAGENTA << "[PNL]" << CLR_RESET << " realized=" << color_num(totals.realized) << " fees=" << totals.fees
                          << " funding=" << color_num(totals.funding) << " upl=" << color_num(totals.unrealized)
                          << " net=" << color_num(net) << "\n";
            }
        }
        catch (const std::exception &ex)
        {
            std::cerr << "[private_ws] parse error: " << ex.what() << " raw=" << msg << "\n";
        } });
    ws->connect();
    ws->subscribe_topics({"privateExecution", "execution", "position"}, "private");
    return ws;
}

void load_env_file(const std::string &path)
{
    std::ifstream infile(path);
    if (!infile.is_open())
        return;
    std::string line;
    while (std::getline(infile, line))
    {
        // Trim leading/trailing spaces.
        auto trim = [](std::string &s)
        {
            const char *ws = " \t\r\n";
            auto start = s.find_first_not_of(ws);
            auto end = s.find_last_not_of(ws);
            if (start == std::string::npos)
            {
                s.clear();
                return;
            }
            s = s.substr(start, end - start + 1);
        };
        trim(line);
        if (line.empty() || line[0] == '#')
            continue;
        auto eq = line.find('=');
        if (eq == std::string::npos || eq == 0)
            continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        trim(key);
        trim(val);
        if (key.empty())
            continue;
        // Only set if not already set in environment.
        if (std::getenv(key.c_str()) == nullptr)
        {
#ifdef _WIN32
            _putenv_s(key.c_str(), val.c_str());
#else
            setenv(key.c_str(), val.c_str(), 0);
#endif
        }
    }
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
    // Load .env if present so BYBIT_* vars can be picked up without exporting.
    load_env_file(".env");

    const std::string symbol = (argc > 1) ? argv[1] : get_env("BYBIT_SYMBOL", "SUIUSDT");
    const std::string api_key = get_env("BYBIT_API_KEY");
    const std::string api_secret = get_env("BYBIT_API_SECRET");
    const std::string base_url = get_env("BYBIT_BASE_URL", "https://api.bybit.com");
    // Use linear for market data and order placement; wallet uses unified explicitly.
    const std::string trade_category = "linear";
    const std::string wallet_category = "UNIFIED";
    const std::string ws_url = get_env("BYBIT_WS_PUBLIC_URL", "wss://stream.bybit.com/v5/public/linear");
    const std::string ws_private_url = get_env("BYBIT_WS_PRIVATE_URL", "wss://stream.bybit.com/v5/private");
    const bool run_live = get_env("BYBIT_RUN_LIVE", "0") == "1";
    const double budget_usd = std::stod(get_env("BYBIT_BUDGET_USD", "10.0"));
    const double min_spread_bps = std::stod(get_env("BYBIT_MIN_SPREAD_BPS", "0.2"));
    const double spread_factor = std::stod(get_env("BYBIT_SPREAD_FACTOR", "1.0"));
    const double max_net_qty = std::stod(get_env("BYBIT_MAX_NET_QTY", "100.0"));
    const double tp_spread_bps = std::stod(get_env("BYBIT_TP_SPREAD_BPS", "0.5"));
    const int ladder_levels = std::stoi(get_env("BYBIT_LADDER_LEVELS", "3"));
    const double stop_loss_bps = std::stod(get_env("BYBIT_STOP_LOSS_BPS", "-1"));
    const double gross_notional_cap = std::stod(get_env("BYBIT_GROSS_NOTIONAL_CAP", "-1"));
    const std::string side_mode = get_env("BYBIT_SIDE_MODE", "both"); // both|long_only

    try
    {
        TradingHelper helper(api_key, api_secret, trade_category, base_url);
        PnlTracker pnl_tracker;
        PositionView pos_view;
        std::mutex pos_mu;
        std::unique_ptr<bybit::WebSocketClient> private_ws;
        if (run_live && helper.has_credentials())
        {
            private_ws = start_private_ws(ws_private_url, api_key, api_secret, pnl_tracker, pos_view, pos_mu);
        }

        // Instrument metadata for sizing/rounding (always query market category linear for perp instruments)
        auto instruments = helper.fetch_instruments_info_for_category("linear");
        std::cout << "[debug] instruments_info fetched\n";
        auto meta_opt = parse_instrument_meta(instruments, symbol);
        if (!meta_opt)
        {
            std::cerr << "Unable to parse instrument meta for " << symbol << "\n";
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

        std::unique_ptr<IStrategy> strategy;
        if (side_mode == "long_only")
        {
            strategy = std::make_unique<LongOnlyMarketMakerStrategy>(symbol, meta, budget_usd, min_spread_bps, spread_factor, 1, 2, max_net_qty, tp_spread_bps, ladder_levels, stop_loss_bps, gross_notional_cap);
        }
        else
        {
            strategy = std::make_unique<ExampleMarketMakerStrategy>(symbol, meta, budget_usd, min_spread_bps, spread_factor, 1, 2, max_net_qty, tp_spread_bps, ladder_levels, stop_loss_bps, gross_notional_cap);
        }

        int i = 0;
        double last_mid = -1.0;
        const double drift_threshold_ticks = 2.0; // cancel/refresh if mid moves this many ticks
        while (true)
        {
            auto ob = feed.latest_orderbook(symbol);
            auto tk = feed.latest_ticker(symbol);
            if (!ob || !tk)
            {
                std::cerr << "Missing data on tick " << i << std::endl;
                break;
            }
            MarketDataSnapshot snap{symbol, *tk, *ob};
            // Detect mid drift vs last iteration to ensure stale orders are refreshed promptly.
            double mid = 0.0;
            try
            {
                const auto &bids = snap.orderbook["b"];
                const auto &asks = snap.orderbook["a"];
                if (!bids.empty() && !asks.empty())
                {
                    double best_bid = std::stod(bids[0][0].get<std::string>());
                    double best_ask = std::stod(asks[0][0].get<std::string>());
                    mid = 0.5 * (best_bid + best_ask);
                }
            }
            catch (...)
            {
            }
            if (last_mid > 0 && mid > 0)
            {
                double ticks_moved = std::abs(mid - last_mid) / meta.tick_size;
                if (ticks_moved >= drift_threshold_ticks && run_live && helper.has_credentials())
                {
                    helper.cancel_all(symbol);
                }
            }
            PositionView pos_snapshot;
            {
                std::lock_guard<std::mutex> lg(pos_mu);
                pos_snapshot = pos_view;
            }
            strategy->on_snapshot(snap, helper, run_live && helper.has_credentials(), pos_snapshot);
            if (mid > 0)
                last_mid = mid;
            if (run_live && helper.has_credentials())
            {
                auto totals = pnl_tracker.totals();
                std::cout << "[PNL] realized=" << color_num(totals.realized) << " fees=" << totals.fees
                          << " funding=" << color_num(totals.funding) << " upl=" << color_num(totals.unrealized)
                          << " net=" << color_num(totals.realized - totals.fees + totals.funding + totals.unrealized) << "\n";
            }
            ++i;
            std::this_thread::sleep_for(std::chrono::seconds{1});
        }

        // Cleanup
        if (run_live && helper.has_credentials())
        {
            helper.cancel_all(symbol);
        }
        if (private_ws)
        {
            private_ws->close();
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
