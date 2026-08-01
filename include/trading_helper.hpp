#pragma once

#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <bybit/rest_client.hpp>

struct MarketDataSnapshot
{
  std::string symbol;
  nlohmann::json ticker;    // raw ticker JSON
  nlohmann::json orderbook; // raw orderbook JSON
};

// TradingHelper wraps bybit::RestClient to provide typed helpers for strategies.
class TradingHelper
{
public:
  TradingHelper(std::string api_key,
                std::string api_secret,
                std::string category = "linear",
                std::string base_url = "https://api.bybit.com");

  // Pulls best bid/ask and ticker snapshot. Throws on HTTP/parse errors.
  MarketDataSnapshot fetch_snapshot(const std::string &symbol, int orderbook_limit = 50);

  // Convenience helpers for individual endpoints.
  nlohmann::json fetch_ticker(const std::string &symbol);
  nlohmann::json fetch_orderbook(const std::string &symbol, int limit = 50);
  nlohmann::json fetch_instruments_info();
  nlohmann::json fetch_instrument_info(const std::string &symbol);
  nlohmann::json fetch_instruments_info_for_category(const std::string &category_override, int limit = 1000);
  nlohmann::json fetch_positions(const std::string &symbol);
  nlohmann::json fetch_fee_rate(const std::string &symbol);
  nlohmann::json fetch_open_orders(const std::string &symbol);
  nlohmann::json enable_disconnect_cancel_all(int time_window_seconds = 10);
  nlohmann::json fetch_disconnect_cancel_all();
  void configure_hedge_mode(const std::string &symbol);

  // Basic order submission helper. Returns raw JSON response as string.
  std::string submit_limit_order(const std::string &symbol,
                                 const std::string &side,
                                 const std::string &qty,
                                 const std::string &price,
                                 int position_idx = 1,
                                 const std::string &order_type = "Limit",
                                 const std::string &order_link_id = "",
                                 const std::string &time_in_force = "PostOnly",
                                 bool reduce_only = false);
  std::string submit_market_order(const std::string &symbol,
                                  const std::string &side,
                                  const std::string &qty,
                                  int position_idx = 1,
                                  const std::string &order_link_id = "",
                                  bool reduce_only = false);
  std::string cancel_all(const std::string &symbol);

  // Batch order operations - all orders in one request
  std::string batch_submit_orders(const std::vector<bybit::JsonObject> &order_requests);
  std::string batch_cancel_orders(const std::vector<bybit::JsonObject> &cancel_requests);
  std::string batch_amend_orders(const std::vector<bybit::JsonObject> &amend_requests);

  bool has_credentials() const { return has_keys_; }

private:
  bool has_keys_;
  std::string category_;
  std::string base_url_;
  std::string api_key_;
  std::string api_secret_;
  std::unique_ptr<bybit::RestClient> rest_client_;
};
