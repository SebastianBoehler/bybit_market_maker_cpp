#pragma once

#include <mutex>
#include <string>
#include <unordered_map>

// Strategy-local PnL tracker keyed by orderLinkId.
// Accumulates realized PnL and fees from private execution stream.
class PnlTracker
{
public:
    struct Totals
    {
        double realized{0.0}; // execPnl
        double fees{0.0};     // trading fees (commission)
        double funding{0.0};  // funding payments
        double unrealized{0.0};
    };

    void add_execution(const std::string &order_link_id, double realized_pnl, double fee)
    {
        std::lock_guard<std::mutex> lg(mu_);
        auto &t = per_order_[order_link_id];
        t.realized += realized_pnl;
        t.fees += fee;
    }

    void add_funding(double funding_payment)
    {
        std::lock_guard<std::mutex> lg(mu_);
        funding_total_ += funding_payment;
    }

    void set_unrealized(const std::string &key, double upl)
    {
        std::lock_guard<std::mutex> lg(mu_);
        unrealized_map_[key] = upl;
    }

    Totals totals() const
    {
        std::lock_guard<std::mutex> lg(mu_);
        Totals agg;
        for (const auto &kv : per_order_)
        {
            agg.realized += kv.second.realized;
            agg.fees += kv.second.fees;
        }
        agg.funding = funding_total_;
        for (const auto &kv : unrealized_map_)
        {
            agg.unrealized += kv.second;
        }
        return agg;
    }

private:
    mutable std::mutex mu_;
    std::unordered_map<std::string, Totals> per_order_;
    double funding_total_{0.0};
    std::unordered_map<std::string, double> unrealized_map_;
};
