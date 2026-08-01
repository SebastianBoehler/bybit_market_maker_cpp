#pragma once

#include <string>
#include <vector>
#include <cstdint>

#include <nlohmann/json.hpp>

#include "order_reconciler.hpp"
#include "strategy_types.hpp"

struct OpenOrderView
{
    std::string order_id;
    std::string order_link_id;
    std::string side;
    std::string order_type;
    double qty{0.0};
    double price{0.0};
    int position_idx{0};
    bool reduce_only{false};
    std::string time_in_force;
    std::string status;
    std::uint64_t created_time{0};
};

struct ClassifiedOpenOrders
{
    std::vector<WorkingOrder> working;
    std::vector<RestingOrder> external;
    std::vector<std::string> cancel_links;
};

struct OpenOrderSnapshot
{
    std::vector<OpenOrderView> orders;
    std::uint64_t generation{0};
    bool seeded{false};
};

PositionView parse_hedge_positions(const nlohmann::json &response, const std::string &symbol);
InstrumentMeta parse_instrument_meta(const nlohmann::json &response, const std::string &symbol);
FeeRates parse_fee_rates(const nlohmann::json &response, const std::string &symbol);
std::vector<RestingOrder> parse_resting_orders(const nlohmann::json &response, const std::string &symbol);
std::vector<OpenOrderView> parse_open_orders(const nlohmann::json &response, const std::string &symbol);
ClassifiedOpenOrders classify_open_orders(const std::vector<OpenOrderView> &orders,
                                          const std::string &application_prefix,
                                          const std::string &session_prefix);
