#include "trading_helper.hpp"

#include <cstdlib>
#include <memory>
#include <optional>
#include <stdexcept>

#include <bybit/rest_client.hpp>

#include "bybit_response.hpp"

namespace
{
    constexpr const char *kDefaultCategory = "linear";
}

TradingHelper::TradingHelper(std::string api_key,
                             std::string api_secret,
                             std::string category,
                             std::string base_url)
    : has_keys_(!api_key.empty() && !api_secret.empty()),
      category_(std::move(category.empty() ? std::string{kDefaultCategory} : category)),
      base_url_(std::move(base_url)),
      api_key_(std::move(api_key)),
      api_secret_(std::move(api_secret))
{
    if (base_url_.empty())
        throw std::runtime_error("TradingHelper base URL must not be empty");
    rest_client_ = std::make_unique<bybit::RestClient>(api_key_, api_secret_, category_, base_url_);
}

nlohmann::json TradingHelper::fetch_ticker(const std::string &symbol)
{
    const auto raw = rest_client_->get_tickers(symbol);
    return validate_bybit_response(raw);
}

nlohmann::json TradingHelper::fetch_orderbook(const std::string &symbol, int limit)
{
    const auto raw = rest_client_->get_orderbook(symbol, limit);
    return validate_bybit_response(raw);
}

nlohmann::json TradingHelper::fetch_instruments_info()
{
    const auto raw = rest_client_->get_instruments_info();
    return validate_bybit_response(raw);
}

nlohmann::json TradingHelper::fetch_instrument_info(const std::string &symbol)
{
    if (symbol.empty())
        throw std::runtime_error("fetch_instrument_info requires an exact symbol");
    return validate_bybit_response(rest_client_->get_instruments_info(std::optional<std::string>{symbol}, 10));
}

nlohmann::json TradingHelper::fetch_instruments_info_for_category(const std::string &category_override, int limit)
{
    // Temporarily construct a short-lived RestClient with the override category, reusing creds/base_url.
    bybit::RestClient temp_client(api_key_, api_secret_, category_override, base_url_);
    const auto raw = temp_client.get_instruments_info(limit);
    return validate_bybit_response(raw);
}

nlohmann::json TradingHelper::fetch_positions(const std::string &symbol)
{
    if (!has_keys_)
        throw std::runtime_error("fetch_positions requires API key/secret");
    return validate_bybit_response(rest_client_->get_position_info(std::nullopt, symbol, 10));
}

nlohmann::json TradingHelper::fetch_fee_rate(const std::string &symbol)
{
    if (!has_keys_)
        throw std::runtime_error("fetch_fee_rate requires API key/secret");
    if (symbol.empty())
        throw std::runtime_error("fetch_fee_rate requires an exact symbol");
    return validate_bybit_response(rest_client_->get_fee_rate(std::optional<std::string>{symbol}));
}

nlohmann::json TradingHelper::fetch_open_orders(const std::string &symbol)
{
    if (!has_keys_)
        throw std::runtime_error("fetch_open_orders requires API key/secret");
    return validate_bybit_response(rest_client_->get_open_orders(symbol, 50));
}

nlohmann::json TradingHelper::enable_disconnect_cancel_all(int time_window_seconds)
{
    if (!has_keys_)
        throw std::runtime_error("enable_disconnect_cancel_all requires API key/secret");
    return validate_bybit_response(rest_client_->set_disconnect_cancel_all(time_window_seconds, "DERIVATIVES"));
}

nlohmann::json TradingHelper::fetch_disconnect_cancel_all()
{
    if (!has_keys_)
        throw std::runtime_error("fetch_disconnect_cancel_all requires API key/secret");
    return validate_bybit_response(rest_client_->get_dcp_info());
}

void TradingHelper::configure_hedge_mode(const std::string &symbol)
{
    if (!has_keys_)
        throw std::runtime_error("configure_hedge_mode requires API key/secret");
    if (symbol.empty())
        throw std::runtime_error("configure_hedge_mode requires an exact symbol");
    validate_bybit_response(rest_client_->switch_position_mode(3, std::optional<std::string>{symbol}),
                            0, {110025});
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
                                              const std::string &order_link_id,
                                              const std::string &time_in_force,
                                              bool reduce_only)
{
    if (!has_keys_)
    {
        throw std::runtime_error("submit_limit_order requires API key/secret");
    }
    const auto response = rest_client_->submit_order(symbol, side, order_type, qty, order_link_id,
                                                     position_idx, price, time_in_force, reduce_only);
    validate_bybit_response(response);
    return response;
}

std::string TradingHelper::submit_market_order(const std::string &symbol,
                                               const std::string &side,
                                               const std::string &qty,
                                               int position_idx,
                                               const std::string &order_link_id,
                                               bool reduce_only)
{
    if (!has_keys_)
    {
        throw std::runtime_error("submit_market_order requires API key/secret");
    }
    const auto response = rest_client_->submit_order(symbol, side, "Market", qty, order_link_id,
                                                     position_idx, "", "IOC", reduce_only);
    validate_bybit_response(response);
    return response;
}

std::string TradingHelper::cancel_all(const std::string &symbol)
{
    if (!has_keys_)
    {
        throw std::runtime_error("cancel_all requires API key/secret");
    }
    const auto response = rest_client_->cancel_all(symbol);
    validate_bybit_response(response);
    return response;
}

std::string TradingHelper::batch_submit_orders(const std::vector<bybit::JsonObject> &order_requests)
{
    if (!has_keys_)
    {
        throw std::runtime_error("batch_submit_orders requires API key/secret");
    }
    const auto response = rest_client_->batch_submit_orders(order_requests);
    validate_bybit_response(response, order_requests.size());
    return response;
}

std::string TradingHelper::batch_cancel_orders(const std::vector<bybit::JsonObject> &cancel_requests)
{
    if (!has_keys_)
    {
        throw std::runtime_error("batch_cancel_orders requires API key/secret");
    }
    const auto response = rest_client_->batch_cancel_orders(cancel_requests);
    validate_bybit_response(response, cancel_requests.size());
    return response;
}

std::string TradingHelper::batch_amend_orders(const std::vector<bybit::JsonObject> &amend_requests)
{
    if (!has_keys_)
    {
        throw std::runtime_error("batch_amend_orders requires API key/secret");
    }
    const auto response = rest_client_->batch_amend_orders(amend_requests);
    validate_bybit_response(response, amend_requests.size());
    return response;
}
