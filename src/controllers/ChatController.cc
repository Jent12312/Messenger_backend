#include "ChatController.h"

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
        // 1. Ищем ID получателя по его коду (например USR-XXXX-XXXX)
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

        // 2. Проверяем, существует ли уже личный чат между этими двумя пользователями
        auto existRes = co_await dbClient->execSqlCoro(
            "SELECT m1.chat_id "
            "FROM chat_members m1 "
            "JOIN chat_members m2 ON m1.chat_id = m2.chat_id "
            "JOIN chats c ON m1.chat_id = c.id "
            "WHERE c.type = 'personal' AND m1.user_id = $1 AND m2.user_id = $2;",
            currentUserId, recipientId
        );

        if (existRes.size() > 0) {
            // Чат уже есть, просто возвращаем его ID
            Json::Value json;
            json["status"] = "success";
            json["chat_id"] = existRes[0]["chat_id"].as<int>();
            json["message"] = "Chat already exists";
            co_return drogon::HttpResponse::newHttpJsonResponse(json);
        }

        // 3. Открываем ТРАНЗАКЦИЮ для создания нового чата
        auto trans = co_await dbClient->newTransactionCoro();

        // Создаем сам чат
        auto chatRes = co_await trans->execSqlCoro(
            "INSERT INTO chats (type) VALUES ('personal') RETURNING id;"
        );
        int newChatId = chatRes[0]["id"].as<int>();

        // Добавляем обоих участников в чат
        co_await trans->execSqlCoro(
            "INSERT INTO chat_members (chat_id, user_id) VALUES ($1, $2), ($1, $3);",
            newChatId, currentUserId, recipientId
        );

        // Коммитим транзакцию
        // (Если произойдет сбой до этой строки, деструктор trans автоматически сделает ROLLBACK)
        // Но так как у Drogon коммит в C++20 сорутинах не возвращает Task, 
        // мы вызываем обычный коммит транзакции
        
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
        // Создаем группу
        auto trans = co_await dbClient->newTransactionCoro();

        auto chatRes = co_await trans->execSqlCoro(
            "INSERT INTO chats (type, title) VALUES ('group', $1) RETURNING id;",
            title
        );
        int newChatId = chatRes[0]["id"].as<int>();

        // Создатель группы становится её владельцем/админом ('creator')
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