#include "order_batch.hpp"

#include <algorithm>

std::vector<OrderRequestBatch> split_order_batches(const OrderRequestBatch &requests)
{
    constexpr std::size_t kBatchLimit = 20;
    std::vector<OrderRequestBatch> batches;
    for (std::size_t offset = 0; offset < requests.size(); offset += kBatchLimit)
    {
        const auto end = std::min(requests.size(), offset + kBatchLimit);
        batches.emplace_back(requests.begin() + static_cast<std::ptrdiff_t>(offset),
                             requests.begin() + static_cast<std::ptrdiff_t>(end));
    }
    return batches;
}

bybit::JsonObject make_create_request(const std::string &symbol,
                                      const PlannedOrder &order,
                                      const std::string &qty,
                                      const std::string &price,
                                      const std::string &link_id)
{
    return {{"symbol", symbol},
            {"side", order.side},
            {"orderType", order.order_type},
            {"qty", qty},
            {"price", price},
            {"timeInForce", order.time_in_force},
            {"orderLinkId", link_id},
            {"positionIdx", order.position_idx},
            {"reduceOnly", order.reduce_only}};
}

bybit::JsonObject make_cancel_request(const std::string &symbol, const std::string &link_id)
{
    return {{"symbol", symbol}, {"orderLinkId", link_id}};
}

bybit::JsonObject make_amend_request(const std::string &symbol,
                                     const std::string &link_id,
                                     const std::string &qty,
                                     const std::string &price)
{
    return {{"symbol", symbol}, {"orderLinkId", link_id}, {"qty", qty}, {"price", price}};
}
