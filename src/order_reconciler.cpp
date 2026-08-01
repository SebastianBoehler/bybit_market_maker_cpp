#include "order_reconciler.hpp"

#include "base36.hpp"

#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace
{
bool immutable_fields_match(const WorkingOrder &working, const PlannedOrder &desired)
{
    return working.side == desired.side && working.order_type == desired.order_type &&
           working.position_idx == desired.position_idx && working.reduce_only == desired.reduce_only &&
           working.time_in_force == desired.time_in_force;
}

bool mutable_fields_match(const WorkingOrder &working, const PlannedOrder &desired)
{
    return std::abs(working.qty - desired.qty) <= 1e-12 &&
           std::abs(working.price - desired.price) <= 1e-12;
}

bool exchange_action_pending(const WorkingOrder &working)
{
    return working.status == "CancelPending" || working.status == "AmendPending";
}
} // namespace

std::string make_order_link(const std::string &prefix,
                            const std::string &name,
                            std::uint64_t counter)
{
    const auto encoded = encode_base36(counter);
    if (name.empty() || prefix.size() + name.size() + encoded.size() + 1 > 36)
        throw std::runtime_error("exact order link prefix/name/counter exceeds Bybit's 36-character limit");
    return prefix + name + "_" + encoded;
}

OrderDelta reconcile_orders(const std::vector<WorkingOrder> &working,
                            const std::vector<PlannedOrder> &desired)
{
    OrderDelta delta;
    std::unordered_map<std::string, const WorkingOrder *> by_name;
    for (const auto &order : working)
        by_name[order.name] = &order;

    std::unordered_set<std::string> desired_names;
    for (const auto &order : desired)
    {
        desired_names.insert(order.name);
        const auto current = by_name.find(order.name);
        if (current == by_name.end())
        {
            delta.create.push_back(order);
            continue;
        }
        if (exchange_action_pending(*current->second))
            continue;
        if (current->second->status == "CreatePending")
        {
            if (!immutable_fields_match(*current->second, order) ||
                !mutable_fields_match(*current->second, order))
                delta.cancel.push_back(*current->second);
            continue;
        }
        if (!immutable_fields_match(*current->second, order))
        {
            delta.cancel.push_back(*current->second);
            continue;
        }
        if (current->second->status != "New")
        {
            delta.cancel.push_back(*current->second);
            continue;
        }
        if (!mutable_fields_match(*current->second, order))
        {
            delta.amend.push_back({current->second->order_link_id, order});
        }
    }
    for (const auto &order : working)
    {
        if (desired_names.count(order.name) == 0 && order.status != "CancelPending" &&
            order.status != "AmendPending")
            delta.cancel.push_back(order);
    }
    return delta;
}
