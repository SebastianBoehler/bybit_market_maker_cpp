#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "bybit_response.hpp"
#include "trading_helper.hpp"

TEST_CASE("nonzero Bybit retCode is rejected even on an HTTP-success body")
{
    CHECK_THROWS_WITH(validate_bybit_response(R"({"retCode":10001,"retMsg":"bad request"})"),
                      Catch::Matchers::ContainsSubstring("10001"));
}

TEST_CASE("a failed batch leg rejects the whole batch response")
{
    const std::string body = R"({
        "retCode": 0,
        "retMsg": "OK",
        "result": {"list": [{"orderId":"a"}, {"orderId":""}]},
        "retExtInfo": {"list": [
            {"code": 0, "msg": "OK"},
            {"code": 110007, "msg": "insufficient balance"}
        ]}
    })";

    CHECK_THROWS_WITH(validate_bybit_response(body, 2),
                      Catch::Matchers::ContainsSubstring("batch leg 1 code 110007"));
}

TEST_CASE("only explicitly allowed idempotent response codes are accepted")
{
    CHECK_NOTHROW(validate_bybit_response(
        R"({"retCode":110025,"retMsg":"Position mode has not been modified"})", 0, {110025}));
    CHECK_THROWS(validate_bybit_response(
        R"({"retCode":110024,"retMsg":"position exists"})", 0, {110025}));
}

TEST_CASE("response codes require complete integer parsing")
{
    CHECK_THROWS(validate_bybit_response(R"({"retCode":"0junk","retMsg":"OK"})"));
    CHECK_THROWS(validate_bybit_response(
        R"({"retCode":0,"result":{"list":[{}]},"retExtInfo":{"list":[{"code":"0junk"}]}})",
        1));
}

TEST_CASE("an explicitly empty REST base URL is never reinterpreted as production")
{
    CHECK_THROWS(TradingHelper("", "", "linear", ""));
}
