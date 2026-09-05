#pragma once
#include <drogon/WebSocketController.h>
#include <unordered_map>
#include <unordered_set>
#include <shared_mutex>
#include <memory>

class WebSocketManager {
public:
    static WebSocketManager& instance() {
        static WebSocketManager inst;
        return inst;
    }

    void addConnection(int userId, const drogon::WebSocketConnectionPtr& conn) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        connections_[userId].insert(conn);
        conn->setContext(std::make_shared<int>(userId));
    }

    void removeConnection(const drogon::WebSocketConnectionPtr& conn) {
        if (conn->hasContext()) {
            auto userIdPtr = conn->getContext<int>();
            if (userIdPtr) {
                int userId = *userIdPtr;
                std::unique_lock<std::shared_mutex> lock(mutex_);
                
                auto it = connections_.find(userId);
                if (it != connections_.end()) {
                    it->second.erase(conn);
                    if (it->second.empty()) {
                        connections_.erase(it);
                    }
                }
            }
        }
    }

    // Проверка статуса "В сети" в оперативной памяти (мгновенно)
    bool isUserOnline(int userId) {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = connections_.find(userId);
        return it != connections_.end() && !it->second.empty();
    }

    void sendToUser(int userId, const Json::Value& message) {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = connections_.find(userId);
        if (it != connections_.end()) {
            Json::StreamWriterBuilder builder;
            builder["emitUTF8"] = true;
            std::string jsonStr = Json::writeString(builder, message);

            for (const auto& conn : it->second) {
                if (conn->connected()) {
                    conn->send(jsonStr);
                }
            }
        }
    }

private:
    std::unordered_map<int, std::unordered_set<drogon::WebSocketConnectionPtr>> connections_;
    std::shared_mutex mutex_;
};