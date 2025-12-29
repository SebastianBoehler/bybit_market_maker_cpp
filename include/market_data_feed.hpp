#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "ws_helper.hpp"

// MarketDataFeed maintains realtime state (ticker + orderbook) via Bybit WebSocket.
// It can be consumed by strategies to get the latest snapshot without re-parsing messages.
class MarketDataFeed
{
public:
    explicit MarketDataFeed(std::string ws_url);
    ~MarketDataFeed();

    // Connect and subscribe to tickers + orderbook (depth=1 by default) for symbols.
    void start(const std::vector<std::string> &symbols, int depth = 1);
    void stop();

    // Wait until at least one ticker AND one orderbook update has been received for any symbol.
    bool wait_for_initial(std::chrono::milliseconds timeout = std::chrono::milliseconds{5000});

    std::optional<nlohmann::json> latest_ticker(const std::string &symbol) const;
    std::optional<nlohmann::json> latest_orderbook(const std::string &symbol) const;

private:
    void handle_message(const std::string &msg);

    WsHelper ws_;
    std::atomic<bool> running_{false};
    std::atomic<bool> got_ticker_{false};
    std::atomic<bool> got_orderbook_{false};

    mutable std::mutex m_;
    std::condition_variable cv_;
    std::unordered_map<std::string, nlohmann::json> tickers_;
    std::unordered_map<std::string, nlohmann::json> orderbooks_;
};
