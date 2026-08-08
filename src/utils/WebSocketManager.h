#pragma once
#include <drogon/WebSocketController.h>
#include <unordered_map>
#include <shared_mutex>
#include <memory>

class WebSocketManager {
public:
    static WebSocketManager& instance() {
        static WebSocketManager inst;
        return inst;
    }

    // Добавить активное сокет-соединение пользователя
    void addConnection(int userId, const drogon::WebSocketConnectionPtr& conn) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        connections_[userId] = conn;
        conn->setContext(std::make_shared<int>(userId));
    }

    // Удалить соединение при отключении
    void removeConnection(const drogon::WebSocketConnectionPtr& conn) {
        if (conn->hasContext()) {
            auto userIdPtr = conn->getContext<int>();
            if (userIdPtr) {
                std::unique_lock<std::shared_mutex> lock(mutex_);
                connections_.erase(*userIdPtr);
            }
        }
    }

    // Отправить JSON конкретному пользователю, если он в сети
    void sendToUser(int userId, const Json::Value& message) {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = connections_.find(userId);
        if (it != connections_.end() && it->second->connected()) {
            it->second->sendJson(message);
        }
    }

private:
    std::unordered_map<int, drogon::WebSocketConnectionPtr> connections_;
    std::shared_mutex mutex_;
};