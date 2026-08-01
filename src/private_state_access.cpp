#include "private_state.hpp"

bool PrivateState::ready() const
{
    return healthy_ && orders_seeded_ && authenticated_ && execution_subscribed_ &&
           position_subscribed_ && order_subscribed_;
}

void PrivateState::mark_unhealthy(std::string reason)
{
    std::lock_guard<std::mutex> lock(mutex_);
    healthy_ = false;
    error_ = std::move(reason);
}

std::string PrivateState::error() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return error_;
}

PositionView PrivateState::position() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return position_;
}

OpenOrderSnapshot PrivateState::order_snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    OpenOrderSnapshot snapshot;
    snapshot.generation = order_generation_;
    snapshot.seeded = orders_seeded_;
    snapshot.orders.reserve(orders_.size());
    for (const auto &[_, order] : orders_)
        snapshot.orders.push_back(order);
    return snapshot;
}

PnlTracker::Totals PrivateState::pnl() const
{
    return pnl_.totals();
}
