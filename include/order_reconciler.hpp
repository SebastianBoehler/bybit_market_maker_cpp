#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "strategy_plan.hpp"

struct WorkingOrder
{
    std::string name;
    std::string order_link_id;
    std::string side;
    std::string order_type;
    double qty{0.0};
    double price{0.0};
    int position_idx{0};
    bool reduce_only{false};
    std::string time_in_force;
    std::string status{"New"};
};

struct AmendOrder
{
    std::string order_link_id;
    PlannedOrder desired;
};

struct OrderDelta
{
    std::vector<WorkingOrder> cancel;
    std::vector<AmendOrder> amend;
    std::vector<PlannedOrder> create;
};

OrderDelta reconcile_orders(const std::vector<WorkingOrder> &working,
                            const std::vector<PlannedOrder> &desired);
std::string make_order_link(const std::string &prefix,
                            const std::string &name,
                            std::uint64_t counter);
