#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <variant>

#include "order_batch.hpp"

namespace
{
const bybit::JsonValue &field(const bybit::JsonObject &object, const std::string &name)
{
    for (const auto &[key, value] : object)
        if (key == name)
            return value;
    throw std::runtime_error("missing request field " + name);
}
} // namespace

TEST_CASE("order requests are split at Bybit's twenty-leg batch limit")
{
    std::vector<bybit::JsonObject> requests(41);

    const auto batches = split_order_batches(requests);

    REQUIRE(batches.size() == 3);
    CHECK(batches[0].size() == 20);
    CHECK(batches[1].size() == 20);
    CHECK(batches[2].size() == 1);
}

TEST_CASE("create request preserves native integer and boolean fields")
{
    const PlannedOrder order{"bid_1", "Buy", "Limit", 0.01, 100.0, 1, false, "PostOnly"};

    const auto request = make_create_request("BTCUSDT", order, "0.01", "100", "owned-link");

    CHECK(std::get<std::int64_t>(field(request, "positionIdx").storage()) == 1);
    CHECK_FALSE(std::get<bool>(field(request, "reduceOnly").storage()));
}
