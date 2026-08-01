#pragma once

#include <chrono>
#include <optional>

class RiskExitPolicy
{
public:
    using Clock = std::chrono::steady_clock;

    bool due(Clock::time_point now) const
    {
        return !last_attempt_ || now - *last_attempt_ >= std::chrono::seconds{2};
    }

    void mark_attempt(Clock::time_point now) { last_attempt_ = now; }
    void clear() { last_attempt_.reset(); }

private:
    std::optional<Clock::time_point> last_attempt_;
};
