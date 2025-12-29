#pragma once

#include <optional>
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
  nlohmann::json fetch_wallet_balance(const std::optional<std::string> &coin = std::nullopt);
  nlohmann::json fetch_wallet_balance_for_category(const std::string &category_override,
                                                   const std::optional<std::string> &coin = std::nullopt);
  nlohmann::json fetch_instruments_info();
  nlohmann::json fetch_instruments_info_for_category(const std::string &category_override, int limit = 1000);

  // Basic order submission helper. Returns raw JSON response as string.
  std::string submit_limit_order(const std::string &symbol,
                                 const std::string &side,
                                 const std::string &qty,
                                 const std::string &price,
                                 int position_idx = 1,
                                 const std::string &order_type = "Limit",
                                 const std::string &order_link_id = "");
  std::string submit_market_order(const std::string &symbol,
                                  const std::string &side,
                                  const std::string &qty,
                                  int position_idx = 1,
                                  const std::string &order_link_id = "");
  std::string cancel_all(const std::string &symbol);

  bool has_credentials() const { return has_keys_; }

private:
  bool has_keys_;
  std::string category_;
  std::string base_url_;
  std::string api_key_;
  std::string api_secret_;
  std::unique_ptr<bybit::RestClient> rest_client_;
};
