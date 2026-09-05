#include "ChatController.h"
#include "../utils/CryptoUtils.h"

drogon::Task<drogon::HttpResponsePtr> ChatController::createPersonalChat(drogon::HttpRequestPtr req) {
    auto dbClient = drogon::app().getDbClient();
    auto jsonBody = req->getJsonObject();

    if (!req->attributes()->find("user_id")) {
        Json::Value json;
        json["status"] = "error";
        json["message"] = "Unauthorized";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
        resp->setStatusCode(drogon::k401Unauthorized);
        co_return resp;
    }

    int currentUserId = std::stoi(req->attributes()->get<std::string>("user_id"));

    if (!jsonBody || !(*jsonBody)["recipient_code"]) {
        Json::Value json;
        json["status"] = "error";
        json["message"] = "Missing recipient_code";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
        resp->setStatusCode(drogon::k400BadRequest);
        co_return resp;
    }

    std::string recipientCode = (*jsonBody)["recipient_code"].asString();

    try {
        auto userRes = co_await dbClient->execSqlCoro("SELECT id FROM users WHERE user_code = $1;", recipientCode);
        if (userRes.size() == 0) {
            Json::Value json;
            json["status"] = "error";
            json["message"] = "Recipient user not found";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
            resp->setStatusCode(drogon::k404NotFound);
            co_return resp;
        }

        int recipientId = userRes[0]["id"].as<int>();

        if (currentUserId == recipientId) {
            Json::Value json;
            json["status"] = "error";
            json["message"] = "You cannot create a chat with yourself";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
            resp->setStatusCode(drogon::k400BadRequest);
            co_return resp;
        }

        auto existRes = co_await dbClient->execSqlCoro(
            "SELECT m1.chat_id "
            "FROM chat_members m1 "
            "JOIN chat_members m2 ON m1.chat_id = m2.chat_id "
            "JOIN chats c ON m1.chat_id = c.id "
            "WHERE c.type = 'personal' AND m1.user_id = $1 AND m2.user_id = $2;",
            currentUserId, recipientId
        );

        if (existRes.size() > 0) {
            Json::Value json;
            json["status"] = "success";
            json["chat_id"] = existRes[0]["chat_id"].as<int>();
            json["message"] = "Chat already exists";
            co_return drogon::HttpResponse::newHttpJsonResponse(json);
        }

        auto trans = co_await dbClient->newTransactionCoro();

        auto chatRes = co_await trans->execSqlCoro(
            "INSERT INTO chats (type) VALUES ('personal') RETURNING id;"
        );
        int newChatId = chatRes[0]["id"].as<int>();

        co_await trans->execSqlCoro(
            "INSERT INTO chat_members (chat_id, user_id) VALUES ($1, $2), ($1, $3);",
            newChatId, currentUserId, recipientId
        );

        Json::Value json;
        json["status"] = "success";
        json["chat_id"] = newChatId;
        json["message"] = "Chat created successfully";
        co_return drogon::HttpResponse::newHttpJsonResponse(json);

    } catch (const std::exception& e) {
        Json::Value json;
        json["status"] = "error";
        json["message"] = "Database error: " + std::string(e.what());
        auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
        resp->setStatusCode(drogon::k500InternalServerError);
        co_return resp;
    }
}

drogon::Task<drogon::HttpResponsePtr> ChatController::createGroupChat(drogon::HttpRequestPtr req) {
    auto dbClient = drogon::app().getDbClient();
    auto jsonBody = req->getJsonObject();

    if (!req->attributes()->find("user_id")) {
        Json::Value json;
        json["status"] = "error";
        json["message"] = "Unauthorized";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
        resp->setStatusCode(drogon::k401Unauthorized);
        co_return resp;
    }

    int currentUserId = std::stoi(req->attributes()->get<std::string>("user_id"));

    if (!jsonBody || !(*jsonBody)["title"]) {
        Json::Value json;
        json["status"] = "error";
        json["message"] = "Missing group title";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
        resp->setStatusCode(drogon::k400BadRequest);
        co_return resp;
    }

    std::string title = (*jsonBody)["title"].asString();

    try {
        auto trans = co_await dbClient->newTransactionCoro();

        auto chatRes = co_await trans->execSqlCoro(
            "INSERT INTO chats (type, title) VALUES ('group', $1) RETURNING id;",
            title
        );
        int newChatId = chatRes[0]["id"].as<int>();

        co_await trans->execSqlCoro(
            "INSERT INTO chat_members (chat_id, user_id, role) VALUES ($1, $2, 'creator');",
            newChatId, currentUserId
        );

        Json::Value json;
        json["status"] = "success";
        json["chat_id"] = newChatId;
        json["message"] = "Group created successfully";
        co_return drogon::HttpResponse::newHttpJsonResponse(json);

    } catch (const std::exception& e) {
        Json::Value json;
        json["status"] = "error";
        json["message"] = "Database error: " + std::string(e.what());
        auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
        resp->setStatusCode(drogon::k500InternalServerError);
        co_return resp;
    }
}

// 1. Получение списка активных чатов (как в Telegram)
drogon::Task<drogon::HttpResponsePtr> ChatController::getChatList(drogon::HttpRequestPtr req) {
    auto dbClient = drogon::app().getDbClient();
    int currentUserId = std::stoi(req->attributes()->get<std::string>("user_id"));

    try {
        // Высокооптимизированный запрос с LATERAL JOIN для быстрого получения последнего сообщения
        auto result = co_await dbClient->execSqlCoro(
            "SELECT "
            "   c.id AS chat_id, "
            "   c.type AS chat_type, "
            "   c.title AS group_title, "
            "   c.avatar_url AS group_avatar, "
            "   cm.is_pinned, "
            "   u.first_name AS partner_first_name, "
            "   u.last_name AS partner_last_name, "
            "   m.text AS last_msg_text, "
            "   m.created_at AS last_msg_time, "
            "   m.is_read AS last_msg_is_read, "
            "   (SELECT COUNT(*) FROM messages msg WHERE msg.chat_id = c.id AND msg.id > cm.last_read_message_id AND msg.sender_id != $1) AS unread_count "
            "FROM chat_members cm "
            "JOIN chats c ON cm.chat_id = c.id "
            "LEFT JOIN chat_members cm_partner ON c.type = 'personal' AND cm_partner.chat_id = c.id AND cm_partner.user_id != $1 "
            "LEFT JOIN users u ON cm_partner.user_id = u.id "
            "LEFT JOIN LATERAL ( "
            "   SELECT text, created_at, is_read FROM messages WHERE chat_id = c.id ORDER BY created_at DESC LIMIT 1 "
            ") m ON true "
            "WHERE cm.user_id = $1 "
            "ORDER BY cm.is_pinned DESC, COALESCE(m.created_at, c.created_at) DESC;",
            currentUserId
        );

        Json::Value jsonChats(Json::arrayValue);

        for (auto row : result) {
            Json::Value chat;
            chat["chat_id"] = row["chat_id"].as<int>();
            chat["type"] = row["chat_type"].as<std::string>();
            chat["is_pinned"] = row["is_pinned"].as<bool>();
            chat["unread_count"] = row["unread_count"].as<int64_t>();

            // Если личный чат — название берется из имени собеседника
            if (row["chat_type"].as<std::string>() == "personal") {
                std::string fName = row["partner_first_name"].isNull() ? "" : row["partner_first_name"].as<std::string>();
                std::string lName = row["partner_last_name"].isNull() ? "" : row["partner_last_name"].as<std::string>();
                chat["title"] = fName + (lName.empty() ? "" : " " + lName);
                chat["avatar_url"] = ""; // Аватарка собеседника
            } else {
                chat["title"] = row["group_title"].isNull() ? "Group" : row["group_title"].as<std::string>();
                chat["avatar_url"] = row["group_avatar"].isNull() ? "" : row["group_avatar"].as<std::string>();
            }

            // Последнее сообщение
            if (!row["last_msg_text"].isNull()) {
                chat["last_message"]["text"] = row["last_msg_text"].as<std::string>();
                chat["last_message"]["time"] = row["last_msg_time"].as<std::string>();
                chat["last_message"]["is_read"] = row["last_msg_is_read"].as<bool>();
            } else {
                chat["last_message"] = Json::Value(Json::nullValue);
            }

            jsonChats.append(chat);
        }

        Json::Value json;
        json["status"] = "success";
        json["chats"] = jsonChats;

        co_return drogon::HttpResponse::newHttpJsonResponse(json);

    } catch (const std::exception& e) {
        Json::Value json;
        json["status"] = "error";
        json["message"] = "Database error: " + std::string(e.what());
        auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
        resp->setStatusCode(drogon::k500InternalServerError);
        co_return resp;
    }
}

// 2. Создание ссылки-приглашения в группу
drogon::Task<drogon::HttpResponsePtr> ChatController::createGroupInvite(drogon::HttpRequestPtr req, std::string chatIdStr) {
    auto dbClient = drogon::app().getDbClient();
    int currentUserId = std::stoi(req->attributes()->get<std::string>("user_id"));
    int chatId = std::stoi(chatIdStr);

    try {
        // Проверяем, что текущий пользователь — админ или создатель этой группы
        auto memberRes = co_await dbClient->execSqlCoro(
            "SELECT role FROM chat_members WHERE chat_id = $1 AND user_id = $2 AND role IN ('admin', 'creator');",
            chatId, currentUserId
        );

        if (memberRes.size() == 0) {
            Json::Value json;
            json["status"] = "error";
            json["message"] = "Forbidden: Only group admins can generate invite links";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
            resp->setStatusCode(drogon::k403Forbidden);
            co_return resp;
        }

        // Генерируем случайный код инвайта (16 символов)
        std::string inviteCode = CryptoUtils::generateSessionToken().substr(0, 16);

        co_await dbClient->execSqlCoro(
            "INSERT INTO chat_invites (chat_id, invite_code, created_by) VALUES ($1, $2, $3);",
            chatId, inviteCode, currentUserId
        );

        Json::Value json;
        json["status"] = "success";
        json["invite_code"] = inviteCode;
        json["invite_url"] = "https://aegischat.jents.online/join/" + inviteCode;

        co_return drogon::HttpResponse::newHttpJsonResponse(json);

    } catch (const std::exception& e) {
        Json::Value json;
        json["status"] = "error";
        json["message"] = "Database error: " + std::string(e.what());
        auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
        resp->setStatusCode(drogon::k500InternalServerError);
        co_return resp;
    }
}

// 3. Вступление в группу по инвайт-коду
drogon::Task<drogon::HttpResponsePtr> ChatController::joinGroupViaInvite(drogon::HttpRequestPtr req, std::string inviteCode) {
    auto dbClient = drogon::app().getDbClient();
    int currentUserId = std::stoi(req->attributes()->get<std::string>("user_id"));

    try {
        // Ищем инвайт в базе
        auto inviteRes = co_await dbClient->execSqlCoro(
            "SELECT chat_id FROM chat_invites WHERE invite_code = $1;",
            inviteCode
        );

        if (inviteRes.size() == 0) {
            Json::Value json;
            json["status"] = "error";
            json["message"] = "Invalid or expired invite code";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
            resp->setStatusCode(drogon::k404NotFound);
            co_return resp;
        }

        int chatId = inviteRes[0]["chat_id"].as<int>();

        // Проверяем, не состоит ли пользователь уже в этой группе
        auto checkMember = co_await dbClient->execSqlCoro(
            "SELECT user_id FROM chat_members WHERE chat_id = $1 AND user_id = $2;",
            chatId, currentUserId
        );

        if (checkMember.size() > 0) {
            Json::Value json;
            json["status"] = "success";
            json["chat_id"] = chatId;
            json["message"] = "You are already a member of this group";
            co_return drogon::HttpResponse::newHttpJsonResponse(json);
        }

        // Добавляем пользователя в группу
        co_await dbClient->execSqlCoro(
            "INSERT INTO chat_members (chat_id, user_id, role) VALUES ($1, $2, 'member');",
            chatId, currentUserId
        );

        Json::Value json;
        json["status"] = "success";
        json["chat_id"] = chatId;
        json["message"] = "Successfully joined the group";

        co_return drogon::HttpResponse::newHttpJsonResponse(json);

    } catch (const std::exception& e) {
        Json::Value json;
        json["status"] = "error";
        json["message"] = "Database error: " + std::string(e.what());
        auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
        resp->setStatusCode(drogon::k500InternalServerError);
        co_return resp;
    }
}

// Получение истории сообщений с пагинацией (before_id)
drogon::Task<drogon::HttpResponsePtr> ChatController::getChatMessages(drogon::HttpRequestPtr req, std::string chatIdStr) {
    auto dbClient = drogon::app().getDbClient();
    
    int currentUserId = 0;
    int chatId = 0;
    int limit = 50;
    int64_t beforeId = 0;

    try {
        currentUserId = std::stoi(req->attributes()->get<std::string>("user_id"));
        chatId = std::stoi(chatIdStr);

        std::string limitStr = req->getParameter("limit");
        std::string beforeIdStr = req->getParameter("before_id");

        if (!limitStr.empty()) limit = std::stoi(limitStr);
        if (limit > 100) limit = 100;
        if (limit < 1) limit = 50;

        if (!beforeIdStr.empty()) beforeId = std::stoll(beforeIdStr);

    } catch (const std::exception&) {
        Json::Value json;
        json["status"] = "error";
        json["message"] = "Bad request: invalid parameter or ID format";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
        resp->setStatusCode(drogon::k400BadRequest);
        co_return resp;
    }

    try {
        auto memberCheck = co_await dbClient->execSqlCoro(
            "SELECT user_id FROM chat_members WHERE chat_id = $1 AND user_id = $2;",
            chatId, currentUserId
        );

        if (memberCheck.size() == 0) {
            Json::Value json;
            json["status"] = "error";
            json["message"] = "Forbidden: You are not a member of this chat";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
            resp->setStatusCode(drogon::k403Forbidden);
            co_return resp;
        }

        // Запрос с получением агрегированных реакций через JSON в PostgreSQL
        std::string baseSql = 
            "SELECT m.id, m.chat_id, m.sender_id, m.text, m.type, m.file_url, m.is_read, m.is_edited, m.created_at, "
            "COALESCE(( "
            "   SELECT json_agg(json_build_object('emoji', r.emoji, 'count', r.cnt)) "
            "   FROM (SELECT emoji, COUNT(*) AS cnt FROM message_reactions WHERE message_id = m.id GROUP BY emoji) r "
            "), '[]'::json) AS reactions "
            "FROM messages m ";

        // Инициализируем result сразу через тернарный оператор (без создания пустого drogon::orm::Result)
        auto result = (beforeId > 0)
            ? co_await dbClient->execSqlCoro(
                baseSql + "WHERE m.chat_id = $1 AND m.id < $2 ORDER BY m.id DESC LIMIT " + std::to_string(limit) + ";",
                chatId, beforeId
            )
            : co_await dbClient->execSqlCoro(
                baseSql + "WHERE m.chat_id = $1 ORDER BY m.id DESC LIMIT " + std::to_string(limit) + ";",
                chatId
            );

        Json::Value jsonMessages(Json::arrayValue);
        for (int i = static_cast<int>(result.size()) - 1; i >= 0; --i) {
            auto row = result[i];
            Json::Value msg;
            msg["id"] = row["id"].as<int64_t>();
            msg["chat_id"] = row["chat_id"].as<int>();
            msg["sender_id"] = row["sender_id"].isNull() ? 0 : row["sender_id"].as<int>();
            msg["text"] = row["text"].as<std::string>();
            msg["type"] = row["type"].as<std::string>();
            msg["file_url"] = row["file_url"].isNull() ? "" : row["file_url"].as<std::string>();
            msg["is_read"] = row["is_read"].as<bool>();
            msg["is_edited"] = row["is_edited"].as<bool>();
            msg["created_at"] = row["created_at"].as<std::string>();

            // Парсим агрегированный JSON реакций от Postgres
            std::string reactionsStr = row["reactions"].as<std::string>();
            Json::Value reactionsJson;
            Json::CharReaderBuilder builder;
            std::string errs;
            std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
            reader->parse(reactionsStr.c_str(), reactionsStr.c_str() + reactionsStr.size(), &reactionsJson, &errs);
            
            msg["reactions"] = reactionsJson;

            jsonMessages.append(msg);
        }

        Json::Value json;
        json["status"] = "success";
        json["messages"] = jsonMessages;

        co_return drogon::HttpResponse::newHttpJsonResponse(json);

    } catch (const std::exception& e) {
        Json::Value json;
        json["status"] = "error";
        json["message"] = "Database error: " + std::string(e.what());
        auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
        resp->setStatusCode(drogon::k500InternalServerError);
        co_return resp;
    }
}