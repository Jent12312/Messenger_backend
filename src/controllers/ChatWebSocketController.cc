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
    if (type != drogon::WebSocketMessageType::Text || !conn->hasContext()) {
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

    // 1. ДЕЙСТВИЕ: ОТПРАВКА СООБЩЕНИЯ
    if (action == "send_message") {
        int chatId = inputJson["chat_id"].asInt();
        std::string text = inputJson["text"].asString();

        if (text.empty()) return;

        drogon::async_run([senderId, chatId, text]() -> drogon::Task<void> {
            auto dbClient = drogon::app().getDbClient();

            try {
                auto msgRes = co_await dbClient->execSqlCoro(
                    "INSERT INTO messages (chat_id, sender_id, text) VALUES ($1, $2, $3) RETURNING id, created_at;",
                    chatId, senderId, text
                );

                int msgId = msgRes[0]["id"].as<int>();
                std::string createdAt = msgRes[0]["created_at"].as<std::string>();

                auto membersRes = co_await dbClient->execSqlCoro(
                    "SELECT user_id FROM chat_members WHERE chat_id = $1;",
                    chatId
                );

                Json::Value outJson;
                outJson["type"] = "new_message";
                outJson["message_id"] = msgId;
                outJson["chat_id"] = chatId;
                outJson["sender_id"] = senderId;
                outJson["text"] = text;
                outJson["created_at"] = createdAt;

                for (auto row : membersRes) {
                    int memberId = row["user_id"].as<int>();
                    WebSocketManager::instance().sendToUser(memberId, outJson);
                }

            } catch (const std::exception& e) {
                LOG_ERROR << "WebSocket send_message error: " << e.what();
            }
        });
    }
    // 2. ДЕЙСТВИЕ: ПРОЧТЕНИЕ СООБЩЕНИЙ ("ГАЛОЧКИ")
    else if (action == "read_message") {
        // Используем 32-битный int (asInt) для стопроцентного совпадения с INT в Postgres
        int chatId = inputJson["chat_id"].asInt();
        int maxMessageId = inputJson["max_message_id"].asInt();

        drogon::async_run([senderId, chatId, maxMessageId]() -> drogon::Task<void> {
            auto dbClient = drogon::app().getDbClient();

            try {
                // Обновляем id последнего прочитанного сообщения у этого пользователя
                co_await dbClient->execSqlCoro(
                    "UPDATE chat_members SET last_read_message_id = GREATEST(last_read_message_id, $1) WHERE chat_id = $2 AND user_id = $3;",
                    maxMessageId, chatId, senderId
                );

                // Помечаем чужие сообщения прочитанными
                co_await dbClient->execSqlCoro(
                    "UPDATE messages SET is_read = TRUE WHERE chat_id = $1 AND id <= $2 AND sender_id != $3 AND is_read = FALSE;",
                    chatId, maxMessageId, senderId
                );

                // Достаем всех участников чата
                auto membersRes = co_await dbClient->execSqlCoro(
                    "SELECT user_id FROM chat_members WHERE chat_id = $1;",
                    chatId
                );

                // Рассылаем событие прочтения всем участникам
                Json::Value readJson;
                readJson["type"] = "messages_read";
                readJson["chat_id"] = chatId;
                readJson["reader_id"] = senderId;
                readJson["max_message_id"] = maxMessageId;

                for (auto row : membersRes) {
                    int memberId = row["user_id"].as<int>();
                    WebSocketManager::instance().sendToUser(memberId, readJson);
                }

            } catch (const std::exception& e) {
                LOG_ERROR << "WebSocket read_message error: " << e.what();
            }
        });
    }
}

void ChatWebSocketController::handleConnectionClosed(const drogon::WebSocketConnectionPtr &conn) {
    WebSocketManager::instance().removeConnection(conn);
}