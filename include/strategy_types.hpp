#pragma once

#include <string>

struct InstrumentMeta
{
    double tick_size{0.0};
    double lot_size{0.0};
    double min_qty{0.0};
    double min_notional{0.0};
    double max_limit_qty{0.0};
    double max_market_qty{0.0};
    double min_price{0.0};
    double max_price{0.0};
};

struct PositionView
{
    double long_size{0.0};
    double short_size{0.0};
    double long_entry{0.0};
    double short_entry{0.0};
};

struct FeeRates
{
    double maker{0.0};
    double taker{0.0};
};

struct RestingOrder
{
    std::string name;
    double qty{0.0};
    double price{0.0};
    bool reduce_only{false};
    std::string side;
};
