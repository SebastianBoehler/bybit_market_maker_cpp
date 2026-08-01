#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>

#include <bybit/websocket_client.hpp>

#include "private_state.hpp"

class PrivateSession
{
public:
    PrivateSession(std::string endpoint,
                   std::string api_key,
                   std::string api_secret,
                   std::string category,
                   PrivateState &state,
                   bool subscribe_dcp);
    ~PrivateSession();

    void start();
    void close();
    bool wait_until_ready(std::chrono::milliseconds timeout);
    bool ready() const;
    bool is_open() const;

private:
    void handle_message(const std::string &message);
    void request_subscriptions();

    std::string category_;
    PrivateState &state_;
    bool subscribe_dcp_{false};
    std::unique_ptr<bybit::WebSocketClient> client_;
    std::atomic<bool> subscriptions_requested_{false};
    mutable std::mutex mutex_;
    std::condition_variable condition_;
};
