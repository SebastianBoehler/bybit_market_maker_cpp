#include "account_state.hpp"

#include "json_value.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>
#include <unordered_map>

namespace
{
bool active_order_status(const std::string &status)
{
    return status == "New" || status == "PartiallyFilled" || status == "Untriggered";
}

bool has_prefix(const std::string &value, const std::string &prefix)
{
    return value.compare(0, prefix.size(), prefix) == 0;
}

bool decimal_text(const std::string &value)
{
    return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char character)
                                         { return std::isdigit(character) != 0; });
}

bool legacy_link_for(const std::string &link_id,
                     const std::string &role,
                     const std::string &marker)
{
    const std::string prefix = role + "_" + marker + "_";
    if (!has_prefix(link_id, prefix))
        return false;
    const auto suffix = link_id.substr(prefix.size());
    const auto separator = suffix.find('_');
    return separator != std::string::npos &&
           decimal_text(suffix.substr(0, separator)) &&
           decimal_text(suffix.substr(separator + 1));
}

bool legacy_bot_link(const std::string &link_id)
{
    constexpr const char *market_maker_roles[] = {
        "bid", "ask", "tp_sell", "tp_buy", "sl_long", "sl_short"};
    constexpr const char *long_only_roles[] = {"bid", "tp_sell", "sl_long"};
    return std::any_of(std::begin(market_maker_roles), std::end(market_maker_roles),
                       [&](const char *role)
                       { return legacy_link_for(link_id, role, "mm"); }) ||
           std::any_of(std::begin(long_only_roles), std::end(long_only_roles),
                       [&](const char *role)
                       { return legacy_link_for(link_id, role, "mmlo"); });
}

std::string owned_name(const std::string &link_id, const std::string &session_prefix)
{
    const auto suffix = link_id.substr(session_prefix.size());
    const auto separator = suffix.rfind('_');
    if (separator == std::string::npos || separator == 0 || separator + 1 == suffix.size())
        return {};
    if (!std::all_of(suffix.begin() + static_cast<std::ptrdiff_t>(separator + 1), suffix.end(),
                     [](unsigned char value)
                     { return std::isdigit(value) != 0 || (value >= 'a' && value <= 'z'); }))
        return {};
    return suffix.substr(0, separator);
}
} // namespace

PositionView parse_hedge_positions(const nlohmann::json &response, const std::string &symbol)
{
    if (!response.contains("result") || !response["result"].contains("list") ||
        !response["result"]["list"].is_array())
        throw std::runtime_error("position response missing result.list");

    PositionView position;
    bool saw_long = false;
    bool saw_short = false;
    for (const auto &row : response["result"]["list"])
    {
        if (row.value("symbol", "") != symbol)
            continue;
        if (!row.contains("positionIdx"))
            throw std::runtime_error("position row missing positionIdx for " + symbol);
        const int index = json_integer(row["positionIdx"], "positionIdx");
        if (index == 0)
            throw std::runtime_error("one-way position mode detected for " + symbol + "; hedge mode is required");
        if (index != 1 && index != 2)
            throw std::runtime_error("unexpected positionIdx for " + symbol);
        const std::string side = row.value("side", "");
        if (index == 1 && !side.empty() && side != "Buy")
            throw std::runtime_error("positionIdx 1 must be Buy for " + symbol);
        if (index == 2 && !side.empty() && side != "Sell")
            throw std::runtime_error("positionIdx 2 must be Sell for " + symbol);
        if (!row.contains("size") || !row.contains("avgPrice"))
            throw std::runtime_error("position row missing size/avgPrice for " + symbol);
        const double size = json_finite_number(row["size"], "position size");
        const double entry = json_finite_number_or_zero(row["avgPrice"], "position avgPrice");
        if (size < 0.0 || entry < 0.0 || (size > 0.0 && entry <= 0.0))
            throw std::runtime_error("position numeric fields invalid for " + symbol);
        if (index == 1)
        {
            saw_long = true;
            position.long_size = size;
            position.long_entry = entry;
        }
        else
        {
            saw_short = true;
            position.short_size = size;
            position.short_entry = entry;
        }
    }
    if (!saw_long || !saw_short)
        throw std::runtime_error("could not establish both hedge-mode legs for " + symbol);
    return position;
}

InstrumentMeta parse_instrument_meta(const nlohmann::json &response, const std::string &symbol)
{
    if (!response.contains("result") || !response["result"].contains("list") ||
        !response["result"]["list"].is_array())
        throw std::runtime_error("instrument response missing result.list");
    for (const auto &row : response["result"]["list"])
    {
        if (row.value("symbol", "") != symbol)
            continue;
        if (!row.contains("priceFilter") || !row["priceFilter"].is_object() ||
            !row.contains("lotSizeFilter") || !row["lotSizeFilter"].is_object())
            throw std::runtime_error("instrument filters missing for " + symbol);
        const auto &price = row["priceFilter"];
        const auto &lot = row["lotSizeFilter"];
        constexpr const char *price_fields[] = {"tickSize", "minPrice", "maxPrice"};
        constexpr const char *lot_fields[] = {"qtyStep", "minNotionalValue", "maxOrderQty", "maxMktOrderQty"};
        for (const auto *field : price_fields)
            if (!price.contains(field))
                throw std::runtime_error("instrument price filter missing " + std::string{field});
        for (const auto *field : lot_fields)
            if (!lot.contains(field))
                throw std::runtime_error("instrument lot filter missing " + std::string{field});
        const char *min_qty_field = lot.contains("minOrderQty") ? "minOrderQty" : "minQty";
        if (!lot.contains(min_qty_field))
            throw std::runtime_error("instrument lot filter missing minimum quantity");
        InstrumentMeta meta{json_finite_number(price["tickSize"], "tickSize"),
                            json_finite_number(lot["qtyStep"], "qtyStep"),
                            json_finite_number(lot[min_qty_field], min_qty_field),
                            json_finite_number(lot["minNotionalValue"], "minNotionalValue"),
                            json_finite_number(lot["maxOrderQty"], "maxOrderQty"),
                            json_finite_number(lot["maxMktOrderQty"], "maxMktOrderQty"),
                            json_finite_number(price["minPrice"], "minPrice"),
                            json_finite_number(price["maxPrice"], "maxPrice")};
        const double fields[] = {meta.tick_size, meta.lot_size, meta.min_qty, meta.min_notional,
                                 meta.max_limit_qty, meta.max_market_qty, meta.min_price, meta.max_price};
        if (!std::all_of(std::begin(fields), std::end(fields),
                         [](double value)
                         { return std::isfinite(value) && value > 0.0; }) ||
            meta.max_limit_qty < meta.min_qty || meta.max_market_qty < meta.min_qty ||
            meta.max_price <= meta.min_price)
            throw std::runtime_error("instrument filters invalid for " + symbol);
        return meta;
    }
    throw std::runtime_error("instrument entry missing for " + symbol);
}

FeeRates parse_fee_rates(const nlohmann::json &response, const std::string &symbol)
{
    if (!response.contains("result") || !response["result"].contains("list") ||
        !response["result"]["list"].is_array())
        throw std::runtime_error("fee response missing result.list");
    for (const auto &row : response["result"]["list"])
    {
        if (row.value("symbol", "") != symbol)
            continue;
        if (!row.contains("makerFeeRate") || !row.contains("takerFeeRate"))
            throw std::runtime_error("fee entry incomplete for " + symbol);
        FeeRates fees{json_finite_number(row["makerFeeRate"], "makerFeeRate"),
                      json_finite_number(row["takerFeeRate"], "takerFeeRate")};
        if (!std::isfinite(fees.maker) || !std::isfinite(fees.taker) ||
            fees.maker <= -1.0 || fees.maker >= 1.0 || fees.taker < 0.0 || fees.taker >= 1.0)
            throw std::runtime_error("fee entry invalid for " + symbol);
        return fees;
    }
    throw std::runtime_error("fee entry missing for " + symbol);
}

std::vector<RestingOrder> parse_resting_orders(const nlohmann::json &response, const std::string &symbol)
{
    std::vector<RestingOrder> orders;
    for (const auto &row : parse_open_orders(response, symbol))
    {
        std::string name = row.order_link_id;
        if (name.empty())
            name = row.order_id.empty() ? "external" : row.order_id;
        orders.push_back({name, row.qty, row.price, row.reduce_only, row.side});
    }
    return orders;
}

std::vector<OpenOrderView> parse_open_orders(const nlohmann::json &response, const std::string &symbol)
{
    if (!response.contains("result") || !response["result"].contains("list") ||
        !response["result"]["list"].is_array())
        throw std::runtime_error("open-order response missing result.list");
    if (!response["result"].value("nextPageCursor", "").empty())
        throw std::runtime_error("open-order response is paginated; complete exchange truth is required");
    std::vector<OpenOrderView> orders;
    for (const auto &row : response["result"]["list"])
    {
        if (row.value("symbol", "") != symbol)
            continue;
        if (!row.contains("orderStatus") || !row["orderStatus"].is_string())
            throw std::runtime_error("open order missing string orderStatus for " + symbol);
        const std::string status = row["orderStatus"].get<std::string>();
        if (!active_order_status(status))
            continue;
        const auto qty_field = row.contains("leavesQty") ? "leavesQty" : "qty";
        if (!row.contains(qty_field) || !row.contains("price"))
            throw std::runtime_error("open order missing remaining qty/price for " + symbol);
        OpenOrderView order;
        order.order_id = row.value("orderId", "");
        order.order_link_id = row.value("orderLinkId", "");
        order.side = row.value("side", "");
        order.order_type = row.value("orderType", "");
        order.qty = json_finite_number(row[qty_field], qty_field);
        order.price = json_finite_number_or_zero(row["price"], "open-order price");
        if (order.qty <= 0.0 || order.price <= 0.0)
            throw std::runtime_error("active open order has non-positive qty/price for " + symbol);
        order.position_idx = row.contains("positionIdx")
                                 ? json_integer(row["positionIdx"], "open-order positionIdx")
                                 : 0;
        order.reduce_only = row.contains("reduceOnly")
                                ? json_boolean(row["reduceOnly"], "open-order reduceOnly")
                                : false;
        order.time_in_force = row.value("timeInForce", "");
        order.status = status;
        order.created_time = row.contains("createdTime")
                                 ? json_unsigned(row["createdTime"], "open-order createdTime")
                                 : 0;
        orders.push_back(std::move(order));
    }
    return orders;
}

ClassifiedOpenOrders classify_open_orders(const std::vector<OpenOrderView> &orders,
                                          const std::string &application_prefix,
                                          const std::string &session_prefix)
{
    ClassifiedOpenOrders result;
    std::unordered_map<std::string, std::vector<const OpenOrderView *>> current;
    for (const auto &order : orders)
    {
        if (legacy_bot_link(order.order_link_id))
        {
            result.cancel_links.push_back(order.order_link_id);
            continue;
        }
        if (!has_prefix(order.order_link_id, application_prefix))
        {
            const auto name = order.order_link_id.empty() ? order.order_id : order.order_link_id;
            result.external.push_back({"external:" + name, order.qty, order.price,
                                       order.reduce_only, order.side});
            continue;
        }
        if (!has_prefix(order.order_link_id, session_prefix))
        {
            result.cancel_links.push_back(order.order_link_id);
            continue;
        }
        const auto name = owned_name(order.order_link_id, session_prefix);
        if (name.empty())
        {
            result.cancel_links.push_back(order.order_link_id);
            continue;
        }
        current[name].push_back(&order);
    }

    for (const auto &[name, matches] : current)
    {
        if (matches.size() != 1)
        {
            for (const auto *order : matches)
                result.cancel_links.push_back(order->order_link_id);
            continue;
        }
        const auto &order = *matches.front();
        result.working.push_back({name, order.order_link_id, order.side, order.order_type,
                                  order.qty, order.price, order.position_idx, order.reduce_only,
                                  order.time_in_force, order.status});
    }
    std::sort(result.cancel_links.begin(), result.cancel_links.end());
    return result;
}
