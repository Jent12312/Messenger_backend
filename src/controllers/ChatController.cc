#include "ChatController.h"
#include "../utils/CryptoUtils.h"
#include <algorithm>

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
        auto result = co_await dbClient->execSqlCoro(
            "SELECT "
            "   c.id AS chat_id, "
            "   c.type AS chat_type, "
            "   c.title AS group_title, "
            "   c.avatar_url AS group_avatar, "
            "   cm.is_pinned, "
            "   u.id AS partner_id, "
            "   u.first_name AS partner_first_name, "
            "   u.last_name AS partner_last_name, "
            "   u.avatar_url AS partner_avatar, "
            "   u.last_seen AS partner_last_seen, "
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

            if (row["chat_type"].as<std::string>() == "personal") {
                std::string fName = row["partner_first_name"].isNull() ? "" : row["partner_first_name"].as<std::string>();
                std::string lName = row["partner_last_name"].isNull() ? "" : row["partner_last_name"].as<std::string>();
                chat["title"] = fName + (lName.empty() ? "" : " " + lName);
                chat["avatar_url"] = row["partner_avatar"].isNull() ? "" : row["partner_avatar"].as<std::string>();

                // СЕТЕВЫЕ СТАТУСЫ СОБЕСЕДНИКА
                if (!row["partner_id"].isNull()) {
                    int partnerId = row["partner_id"].as<int>();
                    chat["is_online"] = WebSocketManager::instance().isUserOnline(partnerId);
                    chat["last_seen"] = row["partner_last_seen"].isNull() ? "" : row["partner_last_seen"].as<std::string>();
                }
            } else {
                chat["title"] = row["group_title"].isNull() ? "Group" : row["group_title"].as<std::string>();
                chat["avatar_url"] = row["group_avatar"].isNull() ? "" : row["group_avatar"].as<std::string>();
            }

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

        // ПРОВЕРКА ЧЕРНОГО СПИСКА (БАН-ЛИСТ)
        auto banCheck = co_await dbClient->execSqlCoro(
            "SELECT 1 FROM chat_banned_users WHERE chat_id = $1 AND user_id = $2;",
            chatId, currentUserId
        );

        if (banCheck.size() > 0) {
            Json::Value json;
            json["status"] = "error";
            json["message"] = "Forbidden: You are banned from this group";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
            resp->setStatusCode(drogon::k403Forbidden);
            co_return resp;
        }

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

// Закрепление / Открепление чата (Pin / Unpin)
drogon::Task<drogon::HttpResponsePtr> ChatController::togglePinChat(drogon::HttpRequestPtr req, std::string chatIdStr) {
    auto dbClient = drogon::app().getDbClient();
    int currentUserId = std::stoi(req->attributes()->get<std::string>("user_id"));
    int chatId = std::stoi(chatIdStr);

    try {
        // Переключаем значение is_pinned на противоположное (NOT is_pinned)
        auto result = co_await dbClient->execSqlCoro(
            "UPDATE chat_members SET is_pinned = NOT is_pinned WHERE chat_id = $1 AND user_id = $2 RETURNING is_pinned;",
            chatId, currentUserId
        );

        if (result.size() == 0) {
            Json::Value json;
            json["status"] = "error";
            json["message"] = "Chat not found or you are not a member";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
            resp->setStatusCode(drogon::k404NotFound);
            co_return resp;
        }

        bool isPinned = result[0]["is_pinned"].as<bool>();

        Json::Value json;
        json["status"] = "success";
        json["is_pinned"] = isPinned;
        json["message"] = isPinned ? "Chat pinned" : "Chat unpinned";

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

// 1. ЗАГРУЗКА МЕДИАФАЙЛОВ В ЧАТ (ДО 500 МБ)
drogon::Task<drogon::HttpResponsePtr> ChatController::uploadChatFile(drogon::HttpRequestPtr req, std::string chatIdStr) {
    auto dbClient = drogon::app().getDbClient();
    int currentUserId = 0;
    int chatId = 0;

    try {
        currentUserId = std::stoi(req->attributes()->get<std::string>("user_id"));
        chatId = std::stoi(chatIdStr);
    } catch (const std::exception&) {
        Json::Value json;
        json["status"] = "error";
        json["message"] = "Invalid chat ID format";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
        resp->setStatusCode(drogon::k400BadRequest);
        co_return resp;
    }

    try {
        // Проверяем членство в чате
        auto memberCheck = co_await dbClient->execSqlCoro(
            "SELECT 1 FROM chat_members WHERE chat_id = $1 AND user_id = $2;",
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

        // Парсинг multipart/form-data
        drogon::MultiPartParser fileUpload;
        if (fileUpload.parse(req) != 0 || fileUpload.getFiles().empty()) {
            Json::Value json;
            json["status"] = "error";
            json["message"] = "Missing file attachment (field 'file')";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
            resp->setStatusCode(drogon::k400BadRequest);
            co_return resp;
        }

        auto file = fileUpload.getFiles()[0];
        std::string ext(file.getFileExtension());
        std::string originalName = file.getFileName();

        // Приводим расширение к нижнему регистру
        std::string lowerExt = ext;
        std::transform(lowerExt.begin(), lowerExt.end(), lowerExt.begin(), ::tolower);

        // Автоматически определяем тип медиа
        std::string msgType = "file";
        if (lowerExt == "jpg" || lowerExt == "jpeg" || lowerExt == "png" || lowerExt == "webp" || lowerExt == "gif") {
            msgType = "image";
        } else if (lowerExt == "mp4" || lowerExt == "webm" || lowerExt == "mov" || lowerExt == "mkv") {
            msgType = "video";
        } else if (lowerExt == "mp3" || lowerExt == "ogg" || lowerExt == "wav" || lowerExt == "m4a") {
            msgType = "voice";
        }

        // Генерируем уникальное имя файла
        std::string uniqueToken = CryptoUtils::generateSessionToken().substr(0, 16);
        std::string newFileName = "chat_" + std::to_string(chatId) + "_" + uniqueToken + "." + lowerExt;

        // Потоковое сохранение в папку uploads/files/
        file.saveAs("files/" + newFileName);

        std::string fileUrl = "https://aegischat.jents.online/uploads/files/" + newFileName;

        Json::Value json;
        json["status"] = "success";
        json["file_url"] = fileUrl;
        json["file_type"] = msgType;
        json["file_name"] = originalName;

        co_return drogon::HttpResponse::newHttpJsonResponse(json);

    } catch (const std::exception& e) {
        Json::Value json;
        json["status"] = "error";
        json["message"] = "Server error: " + std::string(e.what());
        auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
        resp->setStatusCode(drogon::k500InternalServerError);
        co_return resp;
    }
}

// 2. ЗАГРУЗКА АВАТАРКИ ГРУППЫ
drogon::Task<drogon::HttpResponsePtr> ChatController::uploadGroupAvatar(drogon::HttpRequestPtr req, std::string chatIdStr) {
    auto dbClient = drogon::app().getDbClient();
    int currentUserId = 0;
    int chatId = 0;

    try {
        currentUserId = std::stoi(req->attributes()->get<std::string>("user_id"));
        chatId = std::stoi(chatIdStr);
    } catch (const std::exception&) {
        Json::Value json;
        json["status"] = "error";
        json["message"] = "Invalid chat ID format";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
        resp->setStatusCode(drogon::k400BadRequest);
        co_return resp;
    }

    try {
        // Проверяем, что чат — группа, а пользователь — админ или создатель
        auto checkAdmin = co_await dbClient->execSqlCoro(
            "SELECT cm.role FROM chat_members cm "
            "JOIN chats c ON cm.chat_id = c.id "
            "WHERE cm.chat_id = $1 AND cm.user_id = $2 AND c.type = 'group' AND cm.role IN ('admin', 'creator');",
            chatId, currentUserId
        );

        if (checkAdmin.size() == 0) {
            Json::Value json;
            json["status"] = "error";
            json["message"] = "Forbidden: Only group admins can update group avatar";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
            resp->setStatusCode(drogon::k403Forbidden);
            co_return resp;
        }

        drogon::MultiPartParser fileUpload;
        if (fileUpload.parse(req) != 0 || fileUpload.getFiles().empty()) {
            Json::Value json;
            json["status"] = "error";
            json["message"] = "Missing file attachment (field 'avatar')";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
            resp->setStatusCode(drogon::k400BadRequest);
            co_return resp;
        }

        auto file = fileUpload.getFiles()[0];
        std::string ext(file.getFileExtension());
        std::string newFileName = "group_" + std::to_string(chatId) + "_" + CryptoUtils::generateSessionToken().substr(0, 8) + "." + ext;

        file.saveAs("avatars/" + newFileName);
        std::string avatarUrl = "https://aegischat.jents.online/uploads/avatars/" + newFileName;

        co_await dbClient->execSqlCoro("UPDATE chats SET avatar_url = $1 WHERE id = $2;", avatarUrl, chatId);

        Json::Value json;
        json["status"] = "success";
        json["avatar_url"] = avatarUrl;
        json["message"] = "Group avatar updated successfully";

        co_return drogon::HttpResponse::newHttpJsonResponse(json);

    } catch (const std::exception& e) {
        Json::Value json;
        json["status"] = "error";
        json["message"] = "Server error: " + std::string(e.what());
        auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
        resp->setStatusCode(drogon::k500InternalServerError);
        co_return resp;
    }
}

// 3. НАЗНАЧЕНИЕ РОЛИ (ADMIN / MEMBER)
drogon::Task<drogon::HttpResponsePtr> ChatController::updateMemberRole(drogon::HttpRequestPtr req, std::string chatIdStr, std::string targetUserIdStr) {
    auto dbClient = drogon::app().getDbClient();
    int currentUserId = std::stoi(req->attributes()->get<std::string>("user_id"));
    int chatId = std::stoi(chatIdStr);
    int targetUserId = std::stoi(targetUserIdStr);

    auto jsonBody = req->getJsonObject();
    if (!jsonBody || !(*jsonBody)["role"]) {
        Json::Value json;
        json["status"] = "error";
        json["message"] = "Missing 'role' field (admin or member)";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
        resp->setStatusCode(drogon::k400BadRequest);
        co_return resp;
    }

    std::string newRole = (*jsonBody)["role"].asString();
    if (newRole != "admin" && newRole != "member") {
        Json::Value json;
        json["status"] = "error";
        json["message"] = "Invalid role. Allowed: admin, member";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
        resp->setStatusCode(drogon::k400BadRequest);
        co_return resp;
    }

    try {
        // Только создатель группы ('creator') может назначать и снимать админов
        auto checkCreator = co_await dbClient->execSqlCoro(
            "SELECT 1 FROM chat_members WHERE chat_id = $1 AND user_id = $2 AND role = 'creator';",
            chatId, currentUserId
        );

        if (checkCreator.size() == 0) {
            Json::Value json;
            json["status"] = "error";
            json["message"] = "Forbidden: Only group creator can change member roles";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
            resp->setStatusCode(drogon::k403Forbidden);
            co_return resp;
        }

        co_await dbClient->execSqlCoro(
            "UPDATE chat_members SET role = $1 WHERE chat_id = $2 AND user_id = $3;",
            newRole, chatId, targetUserId
        );

        Json::Value json;
        json["status"] = "success";
        json["message"] = "Role updated successfully";
        co_return drogon::HttpResponse::newHttpJsonResponse(json);

    } catch (const std::exception& e) {
        Json::Value json;
        json["status"] = "error";
        json["message"] = "Server error: " + std::string(e.what());
        auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
        resp->setStatusCode(drogon::k500InternalServerError);
        co_return resp;
    }
}

// 4. ИСКЛЮЧЕНИЕ УЧАСТНИКА ИЗ ГРУППЫ (KICK)
drogon::Task<drogon::HttpResponsePtr> ChatController::removeMember(drogon::HttpRequestPtr req, std::string chatIdStr, std::string targetUserIdStr) {
    auto dbClient = drogon::app().getDbClient();
    int currentUserId = std::stoi(req->attributes()->get<std::string>("user_id"));
    int chatId = std::stoi(chatIdStr);
    int targetUserId = std::stoi(targetUserIdStr);

    try {
        // Проверяем права удаляющего: админ или создатель
        auto checkAdmin = co_await dbClient->execSqlCoro(
            "SELECT role FROM chat_members WHERE chat_id = $1 AND user_id = $2 AND role IN ('admin', 'creator');",
            chatId, currentUserId
        );

        if (checkAdmin.size() == 0 && currentUserId != targetUserId) {
            Json::Value json;
            json["status"] = "error";
            json["message"] = "Forbidden: Only group admins can kick members";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
            resp->setStatusCode(drogon::k403Forbidden);
            co_return resp;
        }

        // Удаляем участника из chat_members
        co_await dbClient->execSqlCoro(
            "DELETE FROM chat_members WHERE chat_id = $1 AND user_id = $2;",
            chatId, targetUserId
        );

        Json::Value json;
        json["status"] = "success";
        json["message"] = "Member removed successfully";
        co_return drogon::HttpResponse::newHttpJsonResponse(json);

    } catch (const std::exception& e) {
        Json::Value json;
        json["status"] = "error";
        json["message"] = "Server error: " + std::string(e.what());
        auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
        resp->setStatusCode(drogon::k500InternalServerError);
        co_return resp;
    }
}

// 5. ПОЛУЧЕНИЕ ИНФОРМАЦИИ О ГРУППЕ И ЕЁ УЧАСТНИКАХ
drogon::Task<drogon::HttpResponsePtr> ChatController::getGroupDetails(drogon::HttpRequestPtr req, std::string chatIdStr) {
    auto dbClient = drogon::app().getDbClient();
    int currentUserId = std::stoi(req->attributes()->get<std::string>("user_id"));
    int chatId = std::stoi(chatIdStr);

    try {
        // Проверяем членство в группе
        auto memberCheck = co_await dbClient->execSqlCoro(
            "SELECT 1 FROM chat_members WHERE chat_id = $1 AND user_id = $2;",
            chatId, currentUserId
        );

        if (memberCheck.size() == 0) {
            Json::Value json;
            json["status"] = "error";
            json["message"] = "Forbidden: You are not a member of this group";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
            resp->setStatusCode(drogon::k403Forbidden);
            co_return resp;
        }

        // Данные группы
        auto groupRes = co_await dbClient->execSqlCoro(
            "SELECT title, description, avatar_url, created_at FROM chats WHERE id = $1 AND type = 'group';",
            chatId
        );

        if (groupRes.size() == 0) {
            Json::Value json;
            json["status"] = "error";
            json["message"] = "Group not found";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
            resp->setStatusCode(drogon::k404NotFound);
            co_return resp;
        }

        auto groupRow = groupRes[0];

        // Список участников со статусами онлайн
        auto membersRes = co_await dbClient->execSqlCoro(
            "SELECT u.id, u.username, u.first_name, u.last_name, u.user_code, u.avatar_url, u.last_seen, cm.role "
            "FROM chat_members cm "
            "JOIN users u ON cm.user_id = u.id "
            "WHERE cm.chat_id = $1 ORDER BY (cm.role = 'creator') DESC, (cm.role = 'admin') DESC, u.first_name ASC;",
            chatId
        );

        Json::Value membersJson(Json::arrayValue);
        for (auto row : membersRes) {
            Json::Value member;
            int memberId = row["id"].as<int>();
            member["id"] = memberId;
            member["username"] = row["username"].as<std::string>();
            member["first_name"] = row["first_name"].as<std::string>();
            member["last_name"] = row["last_name"].isNull() ? "" : row["last_name"].as<std::string>();
            member["user_code"] = row["user_code"].as<std::string>();
            member["avatar_url"] = row["avatar_url"].isNull() ? "" : row["avatar_url"].as<std::string>();
            member["role"] = row["role"].as<std::string>();
            member["is_online"] = WebSocketManager::instance().isUserOnline(memberId);
            member["last_seen"] = row["last_seen"].isNull() ? "" : row["last_seen"].as<std::string>();

            membersJson.append(member);
        }

        Json::Value json;
        json["status"] = "success";
        json["group"]["id"] = chatId;
        json["group"]["title"] = groupRow["title"].as<std::string>();
        json["group"]["description"] = groupRow["description"].isNull() ? "" : groupRow["description"].as<std::string>();
        json["group"]["avatar_url"] = groupRow["avatar_url"].isNull() ? "" : groupRow["avatar_url"].as<std::string>();
        json["group"]["created_at"] = groupRow["created_at"].as<std::string>();
        json["group"]["members_count"] = static_cast<int>(membersRes.size());
        json["group"]["members"] = membersJson;

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

// 6. ИЗМЕНЕНИЕ ИНФОРМАЦИИ О ГРУППЕ (НАЗВАНИЕ И ОПИСАНИЕ)
drogon::Task<drogon::HttpResponsePtr> ChatController::updateGroupInfo(drogon::HttpRequestPtr req, std::string chatIdStr) {
    auto dbClient = drogon::app().getDbClient();
    int currentUserId = std::stoi(req->attributes()->get<std::string>("user_id"));
    int chatId = std::stoi(chatIdStr);
    auto jsonBody = req->getJsonObject();

    if (!jsonBody || !(*jsonBody)["title"]) {
        Json::Value json;
        json["status"] = "error";
        json["message"] = "Missing group title";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
        resp->setStatusCode(drogon::k400BadRequest);
        co_return resp;
    }

    std::string title = (*jsonBody)["title"].asString();
    std::string description = (*jsonBody)["description"] ? (*jsonBody)["description"].asString() : "";

    try {
        // Только админы или создатель могут менять инфо
        auto checkAdmin = co_await dbClient->execSqlCoro(
            "SELECT 1 FROM chat_members WHERE chat_id = $1 AND user_id = $2 AND role IN ('admin', 'creator');",
            chatId, currentUserId
        );

        if (checkAdmin.size() == 0) {
            Json::Value json;
            json["status"] = "error";
            json["message"] = "Forbidden: Only group admins can edit group settings";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
            resp->setStatusCode(drogon::k403Forbidden);
            co_return resp;
        }

        co_await dbClient->execSqlCoro(
            "UPDATE chats SET title = $1, description = $2 WHERE id = $3 AND type = 'group';",
            title, description, chatId
        );

        Json::Value json;
        json["status"] = "success";
        json["message"] = "Group info updated successfully";

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

// 7. БАН УЧАСТНИКА (ДОБАВЛЕНИЕ В ЧЕРНЫЙ СПИСОК + КИК)
drogon::Task<drogon::HttpResponsePtr> ChatController::banMember(drogon::HttpRequestPtr req, std::string chatIdStr, std::string targetUserIdStr) {
    auto dbClient = drogon::app().getDbClient();
    int currentUserId = std::stoi(req->attributes()->get<std::string>("user_id"));
    int chatId = std::stoi(chatIdStr);
    int targetUserId = std::stoi(targetUserIdStr);

    try {
        auto checkAdmin = co_await dbClient->execSqlCoro(
            "SELECT 1 FROM chat_members WHERE chat_id = $1 AND user_id = $2 AND role IN ('admin', 'creator');",
            chatId, currentUserId
        );

        if (checkAdmin.size() == 0) {
            Json::Value json;
            json["status"] = "error";
            json["message"] = "Forbidden: Only group admins can ban users";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
            resp->setStatusCode(drogon::k403Forbidden);
            co_return resp;
        }

        // Добавляем в бан-лист
        co_await dbClient->execSqlCoro(
            "INSERT INTO chat_banned_users (chat_id, user_id, banned_by) VALUES ($1, $2, $3) ON CONFLICT DO NOTHING;",
            chatId, targetUserId, currentUserId
        );

        // Исключаем из чата
        co_await dbClient->execSqlCoro(
            "DELETE FROM chat_members WHERE chat_id = $1 AND user_id = $2;",
            chatId, targetUserId
        );

        Json::Value json;
        json["status"] = "success";
        json["message"] = "User banned and removed from group";

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

// 8. РАЗБАН УЧАСТНИКА (УДАЛЕНИЕ ИЗ ЧЕРНОГО СПИСКА)
drogon::Task<drogon::HttpResponsePtr> ChatController::unbanMember(drogon::HttpRequestPtr req, std::string chatIdStr, std::string targetUserIdStr) {
    auto dbClient = drogon::app().getDbClient();
    int currentUserId = std::stoi(req->attributes()->get<std::string>("user_id"));
    int chatId = std::stoi(chatIdStr);
    int targetUserId = std::stoi(targetUserIdStr);

    try {
        auto checkAdmin = co_await dbClient->execSqlCoro(
            "SELECT 1 FROM chat_members WHERE chat_id = $1 AND user_id = $2 AND role IN ('admin', 'creator');",
            chatId, currentUserId
        );

        if (checkAdmin.size() == 0) {
            Json::Value json;
            json["status"] = "error";
            json["message"] = "Forbidden: Only group admins can unban users";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
            resp->setStatusCode(drogon::k403Forbidden);
            co_return resp;
        }

        co_await dbClient->execSqlCoro(
            "DELETE FROM chat_banned_users WHERE chat_id = $1 AND user_id = $2;",
            chatId, targetUserId
        );

        Json::Value json;
        json["status"] = "success";
        json["message"] = "User unbanned";

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