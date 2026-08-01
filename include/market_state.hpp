#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

class MarketState
{
public:
    using Clock = std::chrono::steady_clock;

    void set_connected(bool connected);
    bool apply_frame(const std::string &frame, Clock::time_point received_at);
    bool apply(const nlohmann::json &message, Clock::time_point received_at);
    bool ready(const std::string &symbol,
               Clock::time_point now,
               Clock::duration book_max_age,
               Clock::duration mark_max_age = std::chrono::seconds{1}) const;
    std::uint64_t generation() const { return generation_; }
    std::optional<nlohmann::json> ticker(const std::string &symbol) const;
    std::optional<nlohmann::json> orderbook(const std::string &symbol) const;

private:
    void invalidate_book(const std::string &symbol);
    void invalidate_ticker(const std::string &symbol);

    struct Book
    {
        nlohmann::json data;
        std::uint64_t update_id{0};
        std::uint64_t sequence{0};
        Clock::time_point received_at{};
        bool initialized{false};
        bool sequence_valid{false};
    };

    struct Ticker
    {
        nlohmann::json data;
        Clock::time_point received_at{};
        Clock::time_point mark_received_at{};
        bool mark_valid{false};
    };

    bool connected_{false};
    std::uint64_t generation_{0};
    std::unordered_map<std::string, Book> books_;
    std::unordered_map<std::string, Ticker> tickers_;
};
