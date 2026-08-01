#include "dcp_state.hpp"

bool derivatives_dcp_enabled(const nlohmann::json &response)
{
    if (!response.contains("result") || !response["result"].is_object() ||
        !response["result"].contains("dcpInfos") ||
        !response["result"]["dcpInfos"].is_array())
        return false;
    for (const auto &row : response["result"]["dcpInfos"])
    {
        if (!row.is_object() || !row.contains("product") || !row["product"].is_string() ||
            !row.contains("dcpStatus") || !row["dcpStatus"].is_string())
            continue;
        if (row["product"].get<std::string>() == "DERIVATIVES" &&
            row["dcpStatus"].get<std::string>() == "ON")
            return true;
    }
    return false;
}
