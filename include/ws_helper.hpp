#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <bybit/websocket_client.hpp>

class WsHelper
{
public:
    using MessageHandler = std::function<void(const std::string &)>;

    explicit WsHelper(std::string url);

    // Connect and set a handler; starts non-blocking connection.
    void connect(MessageHandler handler);
    void close();
    bool is_open() const;

    // Subscribe to ticker/orderbook for symbols.
    void subscribe_tickers(const std::vector<std::string> &symbols);
    void subscribe_orderbook(const std::vector<std::string> &symbols, int depth = 1);

private:
    std::unique_ptr<bybit::WebSocketClient> client_;
};
