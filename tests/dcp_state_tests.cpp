#include <catch2/catch_test_macros.hpp>

#include "dcp_state.hpp"

TEST_CASE("DCP requires an explicitly enabled derivatives capability row")
{
    CHECK(derivatives_dcp_enabled(
        {{"result", {{"dcpInfos", {{{"product", "SPOT"}, {"dcpStatus", "ON"}},
                                    {{"product", "DERIVATIVES"}, {"dcpStatus", "ON"}}}}}}}));
    CHECK_FALSE(derivatives_dcp_enabled({{"result", {{"dcpInfos", nlohmann::json::array()}}}}));
    CHECK_FALSE(derivatives_dcp_enabled(
        {{"result", {{"dcpInfos", {{{"product", "DERIVATIVES"}, {"dcpStatus", "OFF"}}}}}}}));
    CHECK_FALSE(derivatives_dcp_enabled(
        {{"result", {{"dcpInfos", {{{"product", "SPOT"}, {"dcpStatus", "ON"}}}}}}}));
    CHECK_FALSE(derivatives_dcp_enabled({{"result", {{"dcpInfos", "malformed"}}}}));
    CHECK_FALSE(derivatives_dcp_enabled(nlohmann::json::object()));
}
