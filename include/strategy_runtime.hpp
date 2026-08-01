#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "account_state.hpp"
#include "order_reconciler.hpp"
#include "order_resync.hpp"
#include "quote_mutation_policy.hpp"
#include "risk_exit_policy.hpp"
#include "trading_helper.hpp"

class StrategyRuntime
{
public:
    StrategyRuntime(std::string symbol, std::vector<RestingOrder> external_resting = {});

    void sync_open_orders(const OpenOrderSnapshot &snapshot);
    void on_snapshot(const StrategySettings &settings,
                     const MarketDataSnapshot &snapshot,
                     TradingHelper &helper,
                     bool live_trading,
                     const PositionView &position);
    void cancel_working(TradingHelper &helper);
    const std::string &owned_order_prefix() const { return owned_order_prefix_; }
    bool kill_latched() const { return kill_latched_; }

private:
    friend struct StrategyRuntimeTestAccess;

    std::vector<RestingOrder> resting_exposure() const;
    void apply_exchange_truth(const std::vector<OpenOrderView> &orders, bool authoritative);
    void refresh_exchange_state(TradingHelper &helper);
    void cancel_links(TradingHelper &helper, const std::vector<std::string> &links);
    bool confirm_cancellations(TradingHelper &helper, bool require_all_owned_absent);
    void execute(const StrategyPlan &plan, const InstrumentMeta &instrument, TradingHelper &helper);
    void execute_delta(const OrderDelta &delta, const InstrumentMeta &instrument, TradingHelper &helper);
    std::string make_link(const std::string &name);

    std::string symbol_;
    std::uint64_t session_id_{0};
    std::string application_order_prefix_;
    std::string owned_order_prefix_;
    std::uint64_t order_counter_{0};
    std::vector<WorkingOrder> working_;
    std::vector<RestingOrder> external_resting_;
    std::vector<std::string> cleanup_links_;
    std::unordered_set<std::string> pending_cancels_;
    std::unordered_map<std::string, PlannedOrder> pending_amends_;
    std::unordered_map<std::string, WorkingOrder> pending_creates_;
    OrderResyncPolicy resync_policy_;
    std::uint64_t private_order_generation_{0};
    bool orders_seeded_{false};
    RiskExitPolicy risk_exit_policy_;
    QuoteMutationPolicy quote_mutation_policy_;
    bool kill_latched_{false};
};
