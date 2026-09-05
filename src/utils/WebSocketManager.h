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

    // Добавить сокет в набор подключений пользователя (поддержка мульти-вкладок и устройств)
    void addConnection(int userId, const drogon::WebSocketConnectionPtr& conn) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        connections_[userId].insert(conn);
        conn->setContext(std::make_shared<int>(userId));
    }

    // Удалить конкретный сокет при закрытии вкладки или обрыве связи
    void removeConnection(const drogon::WebSocketConnectionPtr& conn) {
        if (conn->hasContext()) {
            auto userIdPtr = conn->getContext<int>();
            if (userIdPtr) {
                int userId = *userIdPtr;
                std::unique_lock<std::shared_mutex> lock(mutex_);
                
                auto it = connections_.find(userId);
                if (it != connections_.end()) {
                    it->second.erase(conn); // Удаляем только этот конкретный сокет
                    
                    // Если у пользователя больше не осталось активных сокетов — удаляем ключ
                    if (it->second.empty()) {
                        connections_.erase(it);
                    }
                }
            }
        }
    }

    // Отправить JSON НА ВСЕ открытые вкладки и устройства конкретного пользователя
    void sendToUser(int userId, const Json::Value& message) {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = connections_.find(userId);
        if (it != connections_.end()) {
            Json::StreamWriterBuilder builder;
            builder["emitUTF8"] = true; // Сохраняем чистый UTF-8 без \u041f
            std::string jsonStr = Json::writeString(builder, message);

            // Итерируемся по всем сокетам пользователя и рассылаем сообщение
            for (const auto& conn : it->second) {
                if (conn->connected()) {
                    conn->send(jsonStr);
                }
            }
        }
    }

private:
    // Карта: userId -> Множество активных сокетов (вкладок/устройств)
    std::unordered_map<int, std::unordered_set<drogon::WebSocketConnectionPtr>> connections_;
    std::shared_mutex mutex_;
};