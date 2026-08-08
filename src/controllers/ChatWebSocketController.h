#pragma once
#include <drogon/WebSocketController.h>

class ChatWebSocketController : public drogon::WebSocketController<ChatWebSocketController> {
public:
    WS_PATH_LIST_BEGIN
    WS_PATH_ADD("/ws/chat"); // Точка подключения: wss://aegischat.jents.online/ws/chat?token=...
    WS_PATH_LIST_END

    // Вызывается при первом подключении (Handshake)
    void handleNewConnection(const drogon::HttpRequestPtr &req,
                             const drogon::WebSocketConnectionPtr &conn) override;

    // Вызывается при приходе нового сообщения по сокету
    void handleNewMessage(const drogon::WebSocketConnectionPtr &conn,
                          std::string &&message,
                          const drogon::WebSocketMessageType &type) override;

    // Вызывается при обрыве связи
    void handleConnectionClosed(const drogon::WebSocketConnectionPtr &conn) override;
};