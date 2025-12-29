#include "market_data_feed.hpp"

#include <iostream>
#include <optional>

#include <nlohmann/json.hpp>

namespace
{
    bool is_ticker_topic(const std::string &topic) { return topic.rfind("tickers.", 0) == 0; }
    bool is_orderbook_topic(const std::string &topic) { return topic.rfind("orderbook.", 0) == 0; }

    std::string extract_symbol(const std::string &topic)
    {
        auto pos = topic.rfind('.');
        if (pos == std::string::npos || pos + 1 >= topic.size())
            return topic;
        return topic.substr(pos + 1);
    }
} // namespace

MarketDataFeed::MarketDataFeed(std::string ws_url) : ws_(std::move(ws_url)) {}

MarketDataFeed::~MarketDataFeed() { stop(); }

void MarketDataFeed::start(const std::vector<std::string> &symbols, int depth)
{
    if (running_)
        return;
    running_ = true;
    ws_.connect([this](const std::string &msg)
                { handle_message(msg); });
    ws_.subscribe_tickers(symbols);
    ws_.subscribe_orderbook(symbols, depth);
}

void MarketDataFeed::stop()
{
    if (!running_)
        return;
    running_ = false;
    ws_.close();
}

bool MarketDataFeed::wait_for_initial(std::chrono::milliseconds timeout)
{
    std::unique_lock<std::mutex> lk(m_);
    return cv_.wait_until(lk, std::chrono::steady_clock::now() + timeout,
                          [&]
                          { return got_ticker_.load() && got_orderbook_.load(); });
}

std::optional<nlohmann::json> MarketDataFeed::latest_ticker(const std::string &symbol) const
{
    std::lock_guard<std::mutex> lk(m_);
    auto it = tickers_.find(symbol);
    if (it == tickers_.end())
        return std::nullopt;
    return it->second;
}

std::optional<nlohmann::json> MarketDataFeed::latest_orderbook(const std::string &symbol) const
{
    std::lock_guard<std::mutex> lk(m_);
    auto it = orderbooks_.find(symbol);
    if (it == orderbooks_.end())
        return std::nullopt;
    return it->second;
}

void MarketDataFeed::handle_message(const std::string &msg)
{
    try
    {
        auto j = nlohmann::json::parse(msg);
        if (!j.contains("topic"))
            return;
        const std::string topic = j["topic"].get<std::string>();
        const auto symbol = extract_symbol(topic);
        if (!j.contains("data"))
            return;
        const auto &data = j["data"];

        std::lock_guard<std::mutex> lk(m_);
        if (is_ticker_topic(topic))
        {
            tickers_[symbol] = data;
            got_ticker_ = true;
        }
        else if (is_orderbook_topic(topic))
        {
            orderbooks_[symbol] = data;
            got_orderbook_ = true;
        }
        else
        {
            return;
        }
        cv_.notify_all();
    }
    catch (const std::exception &e)
    {
        std::cerr << "Failed to handle WS message: " << e.what() << "\n";
    }
}
