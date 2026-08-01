#include "market_data_feed.hpp"

#include <iostream>
#include <optional>

#include <nlohmann/json.hpp>

MarketDataFeed::MarketDataFeed(std::string ws_url) : ws_(std::move(ws_url)) {}

MarketDataFeed::~MarketDataFeed() { stop(); }

void MarketDataFeed::start(const std::vector<std::string> &symbols, int depth)
{
    if (running_)
        return;
    running_ = true;
    {
        std::lock_guard<std::mutex> lk(m_);
        symbols_ = symbols;
        protocol_failed_ = false;
    }
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
    {
        std::lock_guard<std::mutex> lk(m_);
        state_.set_connected(false);
    }
    cv_.notify_all();
}

bool MarketDataFeed::wait_for_initial(std::chrono::milliseconds timeout)
{
    std::unique_lock<std::mutex> lk(m_);
    const bool finished = cv_.wait_until(lk, std::chrono::steady_clock::now() + timeout,
                                         [&]
                                         {
                                             if (!running_ || protocol_failed_)
                                                 return true;
                                             const auto now = std::chrono::steady_clock::now();
                                             if (!ws_.is_open())
                                                 return false;
                                             for (const auto &symbol : symbols_)
                                             {
                                                 if (!state_.ready(symbol, now, std::chrono::milliseconds{5000}))
                                                     return false;
                                             }
                                             return !symbols_.empty();
                                         });
    return finished && running_ && !protocol_failed_;
}

bool MarketDataFeed::wait_for_update(std::uint64_t after_generation, std::chrono::milliseconds timeout)
{
    std::unique_lock<std::mutex> lock(m_);
    return cv_.wait_until(lock, std::chrono::steady_clock::now() + timeout,
                          [&]
                          { return !running_ || protocol_failed_ ||
                                   state_.generation() > after_generation; });
}

std::uint64_t MarketDataFeed::generation() const
{
    std::lock_guard<std::mutex> lock(m_);
    return state_.generation();
}

bool MarketDataFeed::ready(const std::string &symbol, std::chrono::milliseconds max_age) const
{
    std::lock_guard<std::mutex> lk(m_);
    return running_ && !protocol_failed_ && ws_.is_open() &&
           state_.ready(symbol, std::chrono::steady_clock::now(), max_age);
}

std::optional<nlohmann::json> MarketDataFeed::latest_ticker(const std::string &symbol) const
{
    std::lock_guard<std::mutex> lk(m_);
    return state_.ticker(symbol);
}

std::optional<nlohmann::json> MarketDataFeed::latest_orderbook(const std::string &symbol) const
{
    std::lock_guard<std::mutex> lk(m_);
    return state_.orderbook(symbol);
}

void MarketDataFeed::handle_message(const std::string &msg)
{
    try
    {
        std::lock_guard<std::mutex> lk(m_);
        if (protocol_failed_)
            return;
        const auto generation = state_.generation();
        state_.set_connected(ws_.is_open());
        state_.apply_frame(msg, std::chrono::steady_clock::now());
        if (state_.generation() > generation)
            cv_.notify_all();
    }
    catch (const std::exception &e)
    {
        {
            std::lock_guard<std::mutex> lk(m_);
            protocol_failed_ = true;
            state_.set_connected(false);
        }
        cv_.notify_all();
        std::cerr << "Failed to handle WS message: " << e.what() << "\n";
    }
}
