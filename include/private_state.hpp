#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "account_state.hpp"
#include "pnl_tracker.hpp"
#include "strategy_types.hpp"

struct PrivateRevisions
{
    std::uint64_t position{0};
    std::uint64_t orders{0};
};

class PrivateState
{
public:
    PrivateState(std::string symbol, std::string category, std::string owned_order_prefix);

    void seed(const PositionView &position);
    void seed_orders(const std::vector<OpenOrderView> &orders);
    bool seed_if_unchanged(const PositionView &position,
                           const std::vector<OpenOrderView> &orders,
                           PrivateRevisions expected);
    PrivateRevisions revisions() const;
    void apply(const nlohmann::json &message);
    void mark_unhealthy(std::string reason);
    bool authenticated() const { return authenticated_.load(); }
    bool healthy() const { return healthy_.load(); }
    bool ready() const;
    std::string error() const;
    PositionView position() const;
    OpenOrderSnapshot order_snapshot() const;
    PnlTracker::Totals pnl() const;

private:
    void apply_order_rows(const nlohmann::json &rows);
    void apply_position_rows(const nlohmann::json &rows);

    std::string symbol_;
    std::string category_;
    std::string owned_order_prefix_;
    mutable std::mutex mutex_;
    PositionView position_;
    std::unordered_map<std::string, OpenOrderView> orders_;
    std::uint64_t order_generation_{0};
    std::uint64_t position_event_revision_{0};
    std::uint64_t order_event_revision_{0};
    PnlTracker pnl_;
    std::atomic<bool> authenticated_{false};
    std::atomic<bool> execution_subscribed_{false};
    std::atomic<bool> position_subscribed_{false};
    std::atomic<bool> order_subscribed_{false};
    std::atomic<bool> orders_seeded_{false};
    std::atomic<bool> healthy_{true};
    std::string error_;
};
