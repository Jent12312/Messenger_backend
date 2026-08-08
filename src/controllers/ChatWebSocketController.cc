#include "ChatWebSocketController.h"
#include "../utils/WebSocketManager.h"
#include <drogon/drogon.h>
#include <drogon/utils/coroutine.h>

void ChatWebSocketController::handleNewConnection(const drogon::HttpRequestPtr &req,
                                                  const drogon::WebSocketConnectionPtr &conn) {
    std::string token = req->getParameter("token");

    if (token.empty()) {
        conn->shutdown(drogon::CloseCode::kPolicyViolation, "Missing token");
        return;
    }

    auto redisClient = drogon::app().getRedisClient();
    std::string redisKey = "session:" + token;

    redisClient->execCommandAsync(
        [conn](const drogon::nosql::RedisResult &r) {
            if (r.type() == drogon::nosql::RedisResultType::kNil) {
                conn->shutdown(drogon::CloseCode::kPolicyViolation, "Invalid or expired token");
                return;
            }

            int userId = std::stoi(r.asString());
            
            // Сохраняем соединение в наш менеджер
            WebSocketManager::instance().addConnection(userId, conn);

            Json::Value response;
            response["type"] = "connection_ack";
            response["status"] = "connected";
            response["user_id"] = userId;
            conn->sendJson(response);
        },
        [conn](const drogon::nosql::RedisException &e) {
            conn->shutdown(drogon::CloseCode::kUnexpectedCondition, "Internal error");
        },
        "GET %s", redisKey.c_str()
    );
}

void ChatWebSocketController::handleNewMessage(const drogon::WebSocketConnectionPtr &conn,
                                               std::string &&message,
                                               const drogon::WebSocketMessageType &type) {
    if (type != drogon::WebSocketMessageType::Text) {
        return;
    }

    if (!conn->hasContext()) {
        return;
    }
    int senderId = *(conn->getContext<int>());

    Json::Value inputJson;
    Json::CharReaderBuilder builder;
    std::string errs;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());

    if (!reader->parse(message.c_str(), message.c_str() + message.size(), &inputJson, &errs)) {
        return;
    }

    std::string action = inputJson["action"].asString();

    if (action == "send_message") {
        int chatId = inputJson["chat_id"].asInt();
        std::string text = inputJson["text"].asString();

        if (text.empty()) return;

        // Запускаем асинхронную сорутину (исправлено на async_run)
        drogon::async_run([senderId, chatId, text]() -> drogon::Task<void> {
            auto dbClient = drogon::app().getDbClient();

            try {
                // 1. Сохраняем сообщение в PostgreSQL
                auto msgRes = co_await dbClient->execSqlCoro(
                    "INSERT INTO messages (chat_id, sender_id, text) VALUES ($1, $2, $3) RETURNING id, created_at;",
                    chatId, senderId, text
                );

                int msgId = msgRes[0]["id"].as<int>();
                std::string createdAt = msgRes[0]["created_at"].as<std::string>();

                // 2. Достаем всех участников этого чата
                auto membersRes = co_await dbClient->execSqlCoro(
                    "SELECT user_id FROM chat_members WHERE chat_id = $1;",
                    chatId
                );

                // 3. Формируем JSON сообщения для рассылки
                Json::Value outJson;
                outJson["type"] = "new_message";
                outJson["message_id"] = msgId;
                outJson["chat_id"] = chatId;
                outJson["sender_id"] = senderId;
                outJson["text"] = text;
                outJson["created_at"] = createdAt;

                // 4. Мгновенно рассылаем сообщение всем онлайн-участникам
                for (auto row : membersRes) {
                    int memberId = row["user_id"].as<int>();
                    WebSocketManager::instance().sendToUser(memberId, outJson);
                }

            } catch (const std::exception& e) {
                LOG_ERROR << "WebSocket send_message error: " << e.what();
            }
        });
    }
}

void ChatWebSocketController::handleConnectionClosed(const drogon::WebSocketConnectionPtr &conn) {
    WebSocketManager::instance().removeConnection(conn);
}