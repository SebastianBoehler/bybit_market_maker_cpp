#include "private_session.hpp"

#include <nlohmann/json.hpp>

PrivateSession::PrivateSession(std::string endpoint,
                               std::string api_key,
                               std::string api_secret,
                               std::string category,
                               PrivateState &state,
                               bool subscribe_dcp)
    : category_(std::move(category)), state_(state), subscribe_dcp_(subscribe_dcp),
      client_(std::make_unique<bybit::WebSocketClient>(std::move(endpoint),
                                                       std::move(api_key),
                                                       std::move(api_secret)))
{
    client_->enable_auto_reconnect(false);
}

PrivateSession::~PrivateSession()
{
    close();
}

void PrivateSession::start()
{
    client_->set_message_handler([this](const std::string &message)
                                 { handle_message(message); });
    client_->connect();
}

void PrivateSession::close()
{
    if (client_)
        client_->close();
    condition_.notify_all();
}

bool PrivateSession::wait_until_ready(std::chrono::milliseconds timeout)
{
    std::unique_lock<std::mutex> lock(mutex_);
    condition_.wait_until(lock, std::chrono::steady_clock::now() + timeout,
                          [&]
                          { return ready() || !state_.healthy(); });
    return ready();
}

bool PrivateSession::ready() const
{
    return client_ && client_->is_open() && state_.ready();
}

bool PrivateSession::is_open() const
{
    return client_ && client_->is_open();
}

void PrivateSession::request_subscriptions()
{
    if (subscriptions_requested_.exchange(true))
        return;
    client_->subscribe_private_execution(category_, "mm-execution");
    client_->subscribe_private_position(category_, "mm-position");
    client_->subscribe_private_order(category_, "mm-order");
    if (subscribe_dcp_)
        client_->subscribe_topic("dcp.future", "mm-dcp");
}

void PrivateSession::handle_message(const std::string &message)
{
    try
    {
        const auto parsed = nlohmann::json::parse(message);
        state_.apply(parsed);
        if (parsed.value("op", "") == "auth" && parsed.value("success", false))
            request_subscriptions();
    }
    catch (const std::exception &error)
    {
        state_.mark_unhealthy(error.what());
    }
    condition_.notify_all();
}
