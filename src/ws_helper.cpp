#include "ws_helper.hpp"

#include <bybit/websocket_client.hpp>

WsHelper::WsHelper(std::string url)
{
    client_ = std::make_unique<bybit::WebSocketClient>(std::move(url));
    client_->enable_auto_reconnect(true);
}

void WsHelper::connect(MessageHandler handler)
{
    client_->set_message_handler(std::move(handler));
    client_->connect();
}

void WsHelper::close() { client_->close(); }

bool WsHelper::is_open() const { return client_->is_open(); }

void WsHelper::subscribe_tickers(const std::vector<std::string> &symbols)
{
    client_->subscribe_tickers(symbols);
}

void WsHelper::subscribe_orderbook(const std::vector<std::string> &symbols, int depth)
{
    client_->subscribe_orderbook(symbols, depth);
}
