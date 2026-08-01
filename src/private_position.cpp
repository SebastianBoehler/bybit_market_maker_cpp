#include "private_state.hpp"

#include "json_value.hpp"

#include <stdexcept>
#include <vector>

namespace
{
struct PositionUpdate
{
    int index;
    double size;
    double entry;
    double unrealized;
};
} // namespace

void PrivateState::apply_position_rows(const nlohmann::json &rows)
{
    std::vector<PositionUpdate> updates;
    for (const auto &row : rows)
    {
        if (row.value("symbol", "") != symbol_)
            continue;
        if (!row.contains("positionIdx") || !row.contains("size") ||
            !row.contains("entryPrice"))
            throw std::runtime_error("private position missing positionIdx/size/entryPrice");
        const int index = json_integer(row["positionIdx"], "private positionIdx");
        const std::string side = row.value("side", "");
        if ((index == 1 && !side.empty() && side != "Buy") ||
            (index == 2 && !side.empty() && side != "Sell") ||
            (index != 1 && index != 2))
            throw std::runtime_error("private position hedge index/side mismatch");
        const double size = json_finite_number(row["size"], "private position size");
        const double entry = json_finite_number_or_zero(row["entryPrice"], "private entryPrice");
        const double unrealized = row.contains("unrealisedPnl")
                                      ? json_finite_number_or_zero(row["unrealisedPnl"],
                                                                   "private unrealisedPnl")
                                      : 0.0;
        if (size < 0.0 || entry < 0.0 || (size > 0.0 && entry <= 0.0))
            throw std::runtime_error("private position contains unsafe size/entry");
        updates.push_back({index, size, entry, unrealized});
    }
    if (updates.empty())
        return;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto &update : updates)
        {
            if (update.index == 1)
            {
                position_.long_size = update.size;
                position_.long_entry = update.size == 0.0 ? 0.0 : update.entry;
            }
            else
            {
                position_.short_size = update.size;
                position_.short_entry = update.size == 0.0 ? 0.0 : update.entry;
            }
        }
        ++position_event_revision_;
    }
    for (const auto &update : updates)
        pnl_.set_unrealized(symbol_ + "_" + std::to_string(update.index), update.unrealized);
}
