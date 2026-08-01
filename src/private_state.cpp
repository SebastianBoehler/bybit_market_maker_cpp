#include "private_state.hpp"

#include "json_value.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <stdexcept>
#include <unordered_set>

namespace
{
void require_position_values(const PositionView &position)
{
    const double values[] = {position.long_size, position.short_size,
                             position.long_entry, position.short_entry};
    if (!std::all_of(std::begin(values), std::end(values),
                     [](double value)
                     { return std::isfinite(value) && value >= 0.0; }) ||
        (position.long_size > 0.0 && position.long_entry <= 0.0) ||
        (position.short_size > 0.0 && position.short_entry <= 0.0))
        throw std::runtime_error("private position contains unsafe numeric values");
}

bool active_order_status(const std::string &status)
{
    return status == "New" || status == "PartiallyFilled" || status == "Untriggered";
}

std::string order_key(const OpenOrderView &order)
{
    return order.order_id.empty() ? order.order_link_id : order.order_id;
}

std::string order_key(const nlohmann::json &row)
{
    const auto order_id = row.value("orderId", "");
    return order_id.empty() ? row.value("orderLinkId", "") : order_id;
}

void merge_order(OpenOrderView &order, const nlohmann::json &row)
{
    if (row.contains("orderId"))
        order.order_id = row["orderId"].get<std::string>();
    if (row.contains("orderLinkId"))
        order.order_link_id = row["orderLinkId"].get<std::string>();
    if (row.contains("side"))
        order.side = row["side"].get<std::string>();
    if (row.contains("orderType"))
        order.order_type = row["orderType"].get<std::string>();
    if (row.contains("leavesQty"))
        order.qty = json_finite_number(row["leavesQty"], "private order leavesQty");
    else if (row.contains("qty"))
        order.qty = json_finite_number(row["qty"], "private order qty");
    if (row.contains("price"))
        order.price = json_finite_number_or_zero(row["price"], "private order price");
    if (row.contains("positionIdx"))
        order.position_idx = json_integer(row["positionIdx"], "private order positionIdx");
    if (row.contains("reduceOnly"))
        order.reduce_only = json_boolean(row["reduceOnly"], "private order reduceOnly");
    if (row.contains("timeInForce"))
        order.time_in_force = row["timeInForce"].get<std::string>();
    if (row.contains("orderStatus"))
        order.status = row["orderStatus"].get<std::string>();
    if (row.contains("createdTime"))
        order.created_time = json_unsigned(row["createdTime"], "private order createdTime");
}

void require_active_order_values(const OpenOrderView &order)
{
    if (!std::isfinite(order.qty) || !std::isfinite(order.price) ||
        order.qty <= 0.0 || order.price <= 0.0)
        throw std::runtime_error("private active order has unsafe qty/price");
}
} // namespace

PrivateState::PrivateState(std::string symbol, std::string category, std::string owned_order_prefix)
    : symbol_(std::move(symbol)), category_(std::move(category)), owned_order_prefix_(std::move(owned_order_prefix)) {}

void PrivateState::seed(const PositionView &position)
{
    require_position_values(position);
    std::lock_guard<std::mutex> lock(mutex_);
    position_ = position;
}

void PrivateState::seed_orders(const std::vector<OpenOrderView> &orders)
{
    std::lock_guard<std::mutex> lock(mutex_);
    orders_.clear();
    for (const auto &order : orders)
    {
        const auto key = order_key(order);
        if (key.empty())
            throw std::runtime_error("seeded open order has no identifier");
        require_active_order_values(order);
        orders_[key] = order;
    }
    ++order_generation_;
    orders_seeded_ = true;
}

bool PrivateState::seed_if_unchanged(const PositionView &position,
                                     const std::vector<OpenOrderView> &orders,
                                     PrivateRevisions expected)
{
    require_position_values(position);
    std::unordered_map<std::string, OpenOrderView> seeded_orders;
    for (const auto &order : orders)
    {
        const auto key = order_key(order);
        if (key.empty())
            throw std::runtime_error("seeded open order has no identifier");
        require_active_order_values(order);
        seeded_orders[key] = order;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (position_event_revision_ != expected.position || order_event_revision_ != expected.orders)
        return false;
    position_ = position;
    orders_ = std::move(seeded_orders);
    ++order_generation_;
    orders_seeded_ = true;
    return true;
}

PrivateRevisions PrivateState::revisions() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return {position_event_revision_, order_event_revision_};
}

void PrivateState::apply_order_rows(const nlohmann::json &rows)
{
    std::unordered_map<std::string, OpenOrderView> candidate;
    std::uint64_t expected_generation = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        candidate = orders_;
        expected_generation = order_generation_;
    }

    bool updated = false;
    for (const auto &row : rows)
    {
        if (row.value("symbol", "") != symbol_)
            continue;
        const auto key = order_key(row);
        if (key.empty())
            throw std::runtime_error("private order update has no identifier");
        if (!row.contains("orderStatus") || !row["orderStatus"].is_string())
            throw std::runtime_error("private order update missing string orderStatus");
        const auto status = row["orderStatus"].get<std::string>();
        if (!active_order_status(status))
            candidate.erase(key);
        else
        {
            auto updated_order = candidate[key];
            merge_order(updated_order, row);
            require_active_order_values(updated_order);
            candidate[key] = std::move(updated_order);
        }
        updated = true;
    }
    if (!updated)
        return;

    std::lock_guard<std::mutex> lock(mutex_);
    if (order_generation_ != expected_generation)
        throw std::runtime_error("private order truth changed during atomic event reduction");
    orders_ = std::move(candidate);
    ++order_generation_;
    ++order_event_revision_;
}

void PrivateState::apply(const nlohmann::json &message)
{
    const std::string operation = message.value("op", "");
    if (operation == "auth")
    {
        const bool success = message.value("success", false);
        authenticated_ = success;
        if (!success)
            mark_unhealthy("private websocket authentication failed");
        return;
    }
    if (operation == "subscribe")
    {
        const bool success = message.value("success", false);
        const std::string request_id = message.value("req_id", "");
        if (request_id == "mm-execution")
            execution_subscribed_ = success;
        else if (request_id == "mm-position")
            position_subscribed_ = success;
        else if (request_id == "mm-order")
            order_subscribed_ = success;
        else if (request_id == "resub" && success)
        {
            std::unordered_set<std::string> topics;
            if (message.contains("args") && message["args"].is_array())
            {
                for (const auto &topic : message["args"])
                    topics.insert(topic.get<std::string>());
            }
            const bool no_topic_echo = topics.empty();
            execution_subscribed_ = no_topic_echo || topics.count("execution." + category_) != 0;
            position_subscribed_ = no_topic_echo || topics.count("position." + category_) != 0;
            order_subscribed_ = no_topic_echo || topics.count("order." + category_) != 0;
        }
        if (!success)
            mark_unhealthy("private websocket subscription failed: " + request_id);
        return;
    }

    if (message.value("topic", "") == "execution." + category_ && message.contains("data") &&
        message["data"].is_array())
    {
        for (const auto &row : message["data"])
        {
            if (row.value("symbol", "") != symbol_)
                continue;
            const std::string link = row.value("orderLinkId", "");
            const std::string execution_id = row.value("execId", "");
            const std::string execution_type = row.value("execType", "Trade");
            if (execution_type == "Funding")
            {
                if (execution_id.empty() || !row.contains("execFee"))
                    throw std::runtime_error("funding execution missing execId/execFee");
                // Bybit reports paid funding as positive execFee; PnL cash flow has the opposite sign.
                pnl_.add_funding(execution_id,
                                 -json_finite_number(row["execFee"], "funding execFee"));
                continue;
            }
            if (link.rfind(owned_order_prefix_, 0) != 0)
                continue;
            if (execution_id.empty())
                throw std::runtime_error("owned execution missing execId");
            const double realized = row.contains("execPnl")
                                        ? json_finite_number(row["execPnl"], "execution execPnl")
                                        : 0.0;
            const double fee = row.contains("execFee")
                                   ? json_finite_number(row["execFee"], "execution execFee")
                                   : 0.0;
            pnl_.add_execution(execution_id, link, realized, fee);
        }
        return;
    }

    if (message.value("topic", "") == "order." + category_ && message.contains("data") &&
        message["data"].is_array())
    {
        apply_order_rows(message["data"]);
        return;
    }

    if (message.value("topic", "") != "position." + category_ || !message.contains("data") ||
        !message["data"].is_array())
        return;
    apply_position_rows(message["data"]);
}
