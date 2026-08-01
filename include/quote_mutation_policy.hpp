#pragma once

#include <chrono>
#include <optional>

class QuoteMutationPolicy
{
public:
    using Clock = std::chrono::steady_clock;

    bool due(Clock::time_point now) const
    {
        return !last_mutation_ || now - *last_mutation_ >= std::chrono::milliseconds{200};
    }

    void mark_mutated(Clock::time_point now) { last_mutation_ = now; }

private:
    std::optional<Clock::time_point> last_mutation_;
};
