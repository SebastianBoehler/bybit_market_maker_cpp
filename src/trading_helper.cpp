#include "trading_helper.hpp"

#include <cstdlib>
#include <memory>
#include <optional>
#include <stdexcept>

#include <bybit/rest_client.hpp>

namespace
{
    constexpr const char *kDefaultCategory = "linear";
    constexpr const char *kDefaultBaseUrl = "https://api.bybit.com";
}

TradingHelper::TradingHelper(std::string api_key,
                             std::string api_secret,
                             std::string category,
                             std::string base_url)
    : has_keys_(!api_key.empty() && !api_secret.empty()),
      category_(std::move(category.empty() ? std::string{kDefaultCategory} : category)),
      base_url_(std::move(base_url.empty() ? std::string{kDefaultBaseUrl} : base_url)),
      api_key_(std::move(api_key)),
      api_secret_(std::move(api_secret))
{
    rest_client_ = std::make_unique<bybit::RestClient>(api_key_, api_secret_, category_, base_url_);
}

nlohmann::json TradingHelper::fetch_ticker(const std::string &symbol)
{
    const auto raw = rest_client_->get_tickers(symbol);
    return nlohmann::json::parse(raw);
}

nlohmann::json TradingHelper::fetch_orderbook(const std::string &symbol, int limit)
{
    const auto raw = rest_client_->get_orderbook(symbol, limit);
    return nlohmann::json::parse(raw);
}

nlohmann::json TradingHelper::fetch_instruments_info()
{
    const auto raw = rest_client_->get_instruments_info();
    return nlohmann::json::parse(raw);
}

nlohmann::json TradingHelper::fetch_instruments_info_for_category(const std::string &category_override, int limit)
{
    // Temporarily construct a short-lived RestClient with the override category, reusing creds/base_url.
    bybit::RestClient temp_client(api_key_, api_secret_, category_override, base_url_);
    const auto raw = temp_client.get_instruments_info(limit);
    return nlohmann::json::parse(raw);
}

nlohmann::json TradingHelper::fetch_wallet_balance(const std::optional<std::string> &coin)
{
    bybit::RestClient temp_client(api_key_, api_secret_, category_, base_url_);
    const auto raw = temp_client.get_wallet_balance(category_, coin);
    return nlohmann::json::parse(raw);
}

nlohmann::json TradingHelper::fetch_wallet_balance_for_category(const std::string &category_override,
                                                                const std::optional<std::string> &coin)
{
    bybit::RestClient temp_client(api_key_, api_secret_, category_override, base_url_);
    const auto raw = temp_client.get_wallet_balance(category_override, coin);
    return nlohmann::json::parse(raw);
}

MarketDataSnapshot TradingHelper::fetch_snapshot(const std::string &symbol, int orderbook_limit)
{
    MarketDataSnapshot snap;
    snap.symbol = symbol;
    snap.ticker = fetch_ticker(symbol);
    snap.orderbook = fetch_orderbook(symbol, orderbook_limit);
    return snap;
}

std::string TradingHelper::submit_limit_order(const std::string &symbol,
                                              const std::string &side,
                                              const std::string &qty,
                                              const std::string &price,
                                              int position_idx,
                                              const std::string &order_type,
                                              const std::string &order_link_id)
{
    if (!has_keys_)
    {
        throw std::runtime_error("submit_limit_order requires API key/secret");
    }
    return rest_client_->submit_order(symbol, side, order_type, qty, order_link_id, position_idx, price);
}

std::string TradingHelper::submit_market_order(const std::string &symbol,
                                               const std::string &side,
                                               const std::string &qty,
                                               int position_idx,
                                               const std::string &order_link_id)
{
    if (!has_keys_)
    {
        throw std::runtime_error("submit_market_order requires API key/secret");
    }
    // price omitted for market; time_in_force left default (GTC acceptable for market per client).
    return rest_client_->submit_order(symbol, side, "Market", qty, order_link_id, position_idx);
}

std::string TradingHelper::cancel_all(const std::string &symbol)
{
    if (!has_keys_)
    {
        throw std::runtime_error("cancel_all requires API key/secret");
    }
    return rest_client_->cancel_all(symbol);
}

std::string TradingHelper::batch_submit_orders(const std::vector<std::vector<std::pair<std::string, std::string>>> &order_requests)
{
    if (!has_keys_)
    {
        throw std::runtime_error("batch_submit_orders requires API key/secret");
    }
    return rest_client_->batch_submit_orders(order_requests);
}

std::string TradingHelper::batch_cancel_orders(const std::vector<std::vector<std::pair<std::string, std::string>>> &cancel_requests)
{
    if (!has_keys_)
    {
        throw std::runtime_error("batch_cancel_orders requires API key/secret");
    }
    return rest_client_->batch_cancel_orders(cancel_requests);
}
