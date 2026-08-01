#pragma once

#include <string>
#include <vector>

#include <bybit/rest_client_types.hpp>

#include "strategy_plan.hpp"

using OrderRequestBatch = std::vector<bybit::JsonObject>;

std::vector<OrderRequestBatch> split_order_batches(const OrderRequestBatch &requests);
bybit::JsonObject make_create_request(const std::string &symbol,
                                      const PlannedOrder &order,
                                      const std::string &qty,
                                      const std::string &price,
                                      const std::string &link_id);
bybit::JsonObject make_cancel_request(const std::string &symbol, const std::string &link_id);
bybit::JsonObject make_amend_request(const std::string &symbol,
                                     const std::string &link_id,
                                     const std::string &qty,
                                     const std::string &price);
