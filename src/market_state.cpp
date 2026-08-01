#include "market_state.hpp"

#include "json_value.hpp"

#include <functional>
#include <map>
#include <optional>

namespace
{
std::string symbol_from_topic(const std::string &topic)
{
    const auto separator = topic.rfind('.');
    return separator == std::string::npos ? std::string{} : topic.substr(separator + 1);
}

std::optional<std::uint64_t> unsigned_field(const nlohmann::json &data, const char *name)
{
    if (!data.contains(name))
        return std::nullopt;
    try
    {
        return json_unsigned(data[name], name);
    }
    catch (...)
    {
        return std::nullopt;
    }
}

bool positive_number(const nlohmann::json &data, const char *name)
{
    if (!data.contains(name))
        return false;
    try
    {
        return json_finite_number(data[name], name) > 0.0;
    }
    catch (...)
    {
        return false;
    }
}

bool valid_levels(const nlohmann::json &levels, bool allow_zero_quantity)
{
    if (!levels.is_array())
        return false;
    try
    {
        for (const auto &level : levels)
        {
            if (!level.is_array() || level.size() < 2)
                return false;
            const double price = json_finite_number(level[0], "orderbook price");
            const double qty = json_finite_number(level[1], "orderbook quantity");
            if (price <= 0.0 || qty < 0.0 || (!allow_zero_quantity && qty == 0.0))
                return false;
        }
        return true;
    }
    catch (...)
    {
        return false;
    }
}

template <typename Compare>
nlohmann::json merge_levels(const nlohmann::json &current,
                            const nlohmann::json &delta,
                            Compare compare)
{
    std::map<double, nlohmann::json, Compare> levels(compare);
    for (const auto &level : current)
        levels[json_finite_number(level.at(0), "orderbook price")] = level;
    for (const auto &level : delta)
    {
        const double price = json_finite_number(level.at(0), "orderbook price");
        const double qty = json_finite_number(level.at(1), "orderbook quantity");
        if (qty == 0.0)
            levels.erase(price);
        else
            levels[price] = level;
    }
    auto merged = nlohmann::json::array();
    for (const auto &[_, level] : levels)
        merged.push_back(level);
    return merged;
}
} // namespace

void MarketState::set_connected(bool connected)
{
    if (connected_ == connected)
        return;
    connected_ = connected;
    books_.clear();
    tickers_.clear();
    ++generation_;
}

bool MarketState::apply_frame(const std::string &frame, Clock::time_point received_at)
{
    try
    {
        return apply(nlohmann::json::parse(frame), received_at);
    }
    catch (...)
    {
        set_connected(false);
        throw;
    }
}

void MarketState::invalidate_book(const std::string &symbol)
{
    const auto book = books_.find(symbol);
    if (book == books_.end() || !book->second.initialized || !book->second.sequence_valid)
        return;
    book->second.sequence_valid = false;
    ++generation_;
}

void MarketState::invalidate_ticker(const std::string &symbol)
{
    const auto ticker = tickers_.find(symbol);
    if (ticker == tickers_.end() || !ticker->second.mark_valid)
        return;
    ticker->second.mark_valid = false;
    ++generation_;
}

bool MarketState::apply(const nlohmann::json &message, Clock::time_point received_at)
{
    if (!message.contains("topic") || !message["topic"].is_string() || !message.contains("data"))
        return false;
    const std::string topic = message["topic"].get<std::string>();
    const std::string topic_symbol = symbol_from_topic(topic);
    const auto &data = message["data"];
    const bool ticker_topic = topic.rfind("tickers.", 0) == 0;
    const bool book_topic = topic.rfind("orderbook.", 0) == 0;
    if (!ticker_topic && !book_topic)
        return false;
    auto invalidate_topic = [&]()
    {
        if (ticker_topic)
            invalidate_ticker(topic_symbol);
        else
            invalidate_book(topic_symbol);
        return false;
    };
    if (!data.is_object())
        return invalidate_topic();
    if (!message.contains("type") || !message["type"].is_string())
        return invalidate_topic();
    const std::string type = message["type"].get<std::string>();
    if (type != "snapshot" && type != "delta")
        return invalidate_topic();
    if (ticker_topic)
    {
        if (topic_symbol.empty() ||
            (data.contains("symbol") &&
             (!data["symbol"].is_string() || data["symbol"].get<std::string>() != topic_symbol)))
            return invalidate_topic();
        const bool explicit_mark = data.contains("markPrice");
        const auto existing = tickers_.find(topic_symbol);
        const bool snapshot = type == "snapshot" || existing == tickers_.end();
        if ((snapshot && !explicit_mark) || (explicit_mark && !positive_number(data, "markPrice")))
            return invalidate_topic();
        auto &ticker = tickers_[topic_symbol];
        if (snapshot)
            ticker.data = data;
        else
            ticker.data.update(data);
        ticker.received_at = received_at;
        if (explicit_mark)
        {
            ticker.mark_received_at = received_at;
            ticker.mark_valid = true;
        }
        ++generation_;
        return true;
    }
    if (topic_symbol.empty() ||
        (data.contains("s") && (!data["s"].is_string() || data["s"].get<std::string>() != topic_symbol)))
        return invalidate_topic();
    const std::string &symbol = topic_symbol;
    if (!data.contains("b") || !data.contains("a"))
        return invalidate_topic();

    const auto update_id_field = unsigned_field(data, "u");
    const auto sequence_field = unsigned_field(data, "seq");
    if (!update_id_field || !sequence_field)
        return invalidate_topic();
    const auto update_id = *update_id_field;
    const auto sequence = *sequence_field;
    if (update_id == 0 || sequence == 0)
        return invalidate_topic();
    const bool snapshot = type == "snapshot" || update_id == 1;
    if (!valid_levels(data["b"], !snapshot) || !valid_levels(data["a"], !snapshot))
        return invalidate_topic();

    auto &book = books_[symbol];
    if (snapshot)
    {
        book.data = data;
        book.update_id = update_id;
        book.sequence = sequence;
        book.received_at = received_at;
        book.initialized = true;
        book.sequence_valid = true;
        ++generation_;
        return true;
    }
    if (!book.initialized || update_id <= book.update_id || sequence <= book.sequence)
        return invalidate_topic();
    book.data["b"] = merge_levels(book.data.value("b", nlohmann::json::array()), data["b"], std::greater<double>{});
    book.data["a"] = merge_levels(book.data.value("a", nlohmann::json::array()), data["a"], std::less<double>{});
    book.data["u"] = update_id;
    book.data["seq"] = sequence;
    book.update_id = update_id;
    book.sequence = sequence;
    book.received_at = received_at;
    ++generation_;
    return true;
}

bool MarketState::ready(const std::string &symbol,
                        Clock::time_point now,
                        Clock::duration book_max_age,
                        Clock::duration mark_max_age) const
{
    if (!connected_)
        return false;
    const auto book = books_.find(symbol);
    const auto ticker_it = tickers_.find(symbol);
    if (book == books_.end() || ticker_it == tickers_.end() || !book->second.initialized ||
        !book->second.sequence_valid || !ticker_it->second.mark_valid ||
        !book->second.data.contains("b") || !book->second.data["b"].is_array() ||
        book->second.data["b"].empty() || !book->second.data.contains("a") ||
        !book->second.data["a"].is_array() || book->second.data["a"].empty() ||
        !positive_number(ticker_it->second.data, "markPrice"))
        return false;
    const auto book_age = now - book->second.received_at;
    const auto ticker_age = now - ticker_it->second.mark_received_at;
    return book_age >= Clock::duration::zero() && ticker_age >= Clock::duration::zero() &&
           book_age <= book_max_age && ticker_age <= mark_max_age;
}

std::optional<nlohmann::json> MarketState::ticker(const std::string &symbol) const
{
    const auto it = tickers_.find(symbol);
    if (it == tickers_.end())
        return std::nullopt;
    return it->second.data;
}

std::optional<nlohmann::json> MarketState::orderbook(const std::string &symbol) const
{
    const auto it = books_.find(symbol);
    if (it == books_.end() || !it->second.initialized || !it->second.sequence_valid)
        return std::nullopt;
    return it->second.data;
}
