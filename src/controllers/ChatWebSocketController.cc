#include "ChatWebSocketController.h"
#include "../utils/WebSocketManager.h"
#include <drogon/drogon.h>
#include <drogon/utils/coroutine.h>

void ChatWebSocketController::handleNewConnection(const drogon::HttpRequestPtr &req,
                                                  const drogon::WebSocketConnectionPtr &conn) {
    std::string token = req->getParameter("token");

    if (token.empty()) {
        conn->shutdown(drogon::CloseCode::kViolation, "Missing token");
        return;
    }

    auto redisClient = drogon::app().getRedisClient();
    std::string redisKey = "session:" + token;

    redisClient->execCommandAsync(
        [conn](const drogon::nosql::RedisResult &r) {
            if (r.type() == drogon::nosql::RedisResultType::kNil) {
                conn->shutdown(drogon::CloseCode::kViolation, "Invalid or expired token");
                return;
            }

            try {
                int userId = std::stoi(r.asString());
                WebSocketManager::instance().addConnection(userId, conn);

                Json::Value response;
                response["type"] = "connection_ack";
                response["status"] = "connected";
                response["user_id"] = userId;
                conn->sendJson(response);

            } catch (const std::exception &e) {
                LOG_ERROR << "Corrupted session data in Redis: " << e.what();
                conn->shutdown(drogon::CloseCode::kViolation, "Corrupted session data");
            }
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

    // 1. ОТПРАВКА СООБЩЕНИЯ (поддерживает любые эмодзи 👍❤️🔥)
    if (action == "send_message") {
        int chatId = inputJson["chat_id"].asInt();
        std::string text = inputJson["text"].asString();

        if (text.empty()) return;

        drogon::async_run([senderId, chatId, text]() -> drogon::Task<void> {
            auto dbClient = drogon::app().getDbClient();

            try {
                auto memberCheck = co_await dbClient->execSqlCoro(
                    "SELECT 1 FROM chat_members WHERE chat_id = $1 AND user_id = $2;",
                    chatId, senderId
                );

                if (memberCheck.size() == 0) {
                    Json::Value errJson;
                    errJson["type"] = "error";
                    errJson["message"] = "Forbidden: You are not a member of this chat";
                    WebSocketManager::instance().sendToUser(senderId, errJson);
                    co_return;
                }

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
                outJson["is_edited"] = false;
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
    // 2. РЕДАКТИРОВАНИЕ СООБЩЕНИЯ
    else if (action == "edit_message") {
        int chatId = inputJson["chat_id"].asInt();
        int messageId = inputJson["message_id"].asInt();
        std::string newText = inputJson["text"].asString();

        if (newText.empty()) return;

        drogon::async_run([senderId, chatId, messageId, newText]() -> drogon::Task<void> {
            auto dbClient = drogon::app().getDbClient();

            try {
                // Проверяем, что автор сообщения — именно текущий пользователь
                auto msgCheck = co_await dbClient->execSqlCoro(
                    "SELECT sender_id FROM messages WHERE id = $1 AND chat_id = $2;",
                    messageId, chatId
                );

                if (msgCheck.size() == 0 || msgCheck[0]["sender_id"].as<int>() != senderId) {
                    Json::Value errJson;
                    errJson["type"] = "error";
                    errJson["message"] = "Forbidden: You can only edit your own messages";
                    WebSocketManager::instance().sendToUser(senderId, errJson);
                    co_return;
                }

                // Обновляем текст и флаг is_edited
                co_await dbClient->execSqlCoro(
                    "UPDATE messages SET text = $1, is_edited = TRUE WHERE id = $2;",
                    newText, messageId
                );

                auto membersRes = co_await dbClient->execSqlCoro(
                    "SELECT user_id FROM chat_members WHERE chat_id = $1;",
                    chatId
                );

                Json::Value editJson;
                editJson["type"] = "message_edited";
                editJson["chat_id"] = chatId;
                editJson["message_id"] = messageId;
                editJson["text"] = newText;
                editJson["is_edited"] = true;

                for (auto row : membersRes) {
                    int memberId = row["user_id"].as<int>();
                    WebSocketManager::instance().sendToUser(memberId, editJson);
                }

            } catch (const std::exception& e) {
                LOG_ERROR << "WebSocket edit_message error: " << e.what();
            }
        });
    }
    // 3. РЕАКЦИИ (ЛАЙКИ / ЭМОДЗИ)
    else if (action == "toggle_reaction") {
        int chatId = inputJson["chat_id"].asInt();
        int messageId = inputJson["message_id"].asInt();
        std::string emoji = inputJson["emoji"].asString();

        if (emoji.empty()) return;

        drogon::async_run([senderId, chatId, messageId, emoji]() -> drogon::Task<void> {
            auto dbClient = drogon::app().getDbClient();

            try {
                auto memberCheck = co_await dbClient->execSqlCoro(
                    "SELECT 1 FROM chat_members WHERE chat_id = $1 AND user_id = $2;",
                    chatId, senderId
                );

                if (memberCheck.size() == 0) co_return;

                // Проверяем существование предыдущей реакции
                auto existReaction = co_await dbClient->execSqlCoro(
                    "SELECT emoji FROM message_reactions WHERE message_id = $1 AND user_id = $2;",
                    messageId, senderId
                );

                if (existReaction.size() > 0 && existReaction[0]["emoji"].as<std::string>() == emoji) {
                    // Если пользователь нажал на тот же эмодзи — убираем реакцию (Toggle OFF)
                    co_await dbClient->execSqlCoro(
                        "DELETE FROM message_reactions WHERE message_id = $1 AND user_id = $2;",
                        messageId, senderId
                    );
                } else {
                    // Вставляем или меняем реакцию (UPSERT)
                    co_await dbClient->execSqlCoro(
                        "INSERT INTO message_reactions (message_id, user_id, emoji) VALUES ($1, $2, $3) "
                        "ON CONFLICT (message_id, user_id) DO UPDATE SET emoji = $3;",
                        messageId, senderId, emoji
                    );
                }

                // Считаем обновленную агрегированную статистику реакций
                auto aggregatedRes = co_await dbClient->execSqlCoro(
                    "SELECT emoji, COUNT(*) AS count FROM message_reactions WHERE message_id = $1 GROUP BY emoji;",
                    messageId
                );

                Json::Value reactionsJson(Json::arrayValue);
                for (auto row : aggregatedRes) {
                    Json::Value item;
                    item["emoji"] = row["emoji"].as<std::string>();
                    item["count"] = row["count"].as<int64_t>();
                    reactionsJson.append(item);
                }

                auto membersRes = co_await dbClient->execSqlCoro(
                    "SELECT user_id FROM chat_members WHERE chat_id = $1;",
                    chatId
                );

                Json::Value reactJson;
                reactJson["type"] = "message_reaction";
                reactJson["chat_id"] = chatId;
                reactJson["message_id"] = messageId;
                reactJson["user_id"] = senderId;
                reactJson["reactions"] = reactionsJson;

                for (auto row : membersRes) {
                    int memberId = row["user_id"].as<int>();
                    WebSocketManager::instance().sendToUser(memberId, reactJson);
                }

            } catch (const std::exception& e) {
                LOG_ERROR << "WebSocket toggle_reaction error: " << e.what();
            }
        });
    }
    // 4. ПРОЧТЕНИЕ СООБЩЕНИЙ
    else if (action == "read_message") {
        int chatId = inputJson["chat_id"].asInt();
        int maxMessageId = inputJson["max_message_id"].asInt();

        drogon::async_run([senderId, chatId, maxMessageId]() -> drogon::Task<void> {
            auto dbClient = drogon::app().getDbClient();

            try {
                auto memberCheck = co_await dbClient->execSqlCoro(
                    "SELECT 1 FROM chat_members WHERE chat_id = $1 AND user_id = $2;",
                    chatId, senderId
                );

                if (memberCheck.size() == 0) co_return;

                co_await dbClient->execSqlCoro(
                    "UPDATE chat_members SET last_read_message_id = GREATEST(last_read_message_id, $1) WHERE chat_id = $2 AND user_id = $3;",
                    maxMessageId, chatId, senderId
                );

                co_await dbClient->execSqlCoro(
                    "UPDATE messages SET is_read = TRUE WHERE chat_id = $1 AND id <= $2 AND sender_id != $3 AND is_read = FALSE;",
                    chatId, maxMessageId, senderId
                );

                auto membersRes = co_await dbClient->execSqlCoro(
                    "SELECT user_id FROM chat_members WHERE chat_id = $1;",
                    chatId
                );

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