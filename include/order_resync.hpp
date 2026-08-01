#pragma once

#include <chrono>
#include <optional>

class OrderResyncPolicy
{
public:
    using Clock = std::chrono::steady_clock;

    void mark_synced(Clock::time_point now)
    {
        last_sync_ = now;
        uncertain_since_.reset();
    }

    void mark_uncertain(Clock::time_point now)
    {
        if (!uncertain_since_)
            uncertain_since_ = now;
    }

    void mark_event() { uncertain_since_.reset(); }

    bool due(Clock::time_point now) const
    {
        if (!last_sync_)
            return true;
        if (now - *last_sync_ < kMinimumInterval)
            return false;
        return now - *last_sync_ >= kPeriodicInterval ||
               (uncertain_since_ && now - *uncertain_since_ >= kUncertaintyDelay);
    }

private:
    static constexpr auto kMinimumInterval = std::chrono::seconds{10};
    static constexpr auto kPeriodicInterval = std::chrono::seconds{30};
    static constexpr auto kUncertaintyDelay = std::chrono::seconds{3};

    std::optional<Clock::time_point> last_sync_;
    std::optional<Clock::time_point> uncertain_since_;
};
