#include "UserController.h"

drogon::Task<drogon::HttpResponsePtr> UserController::getProfile(drogon::HttpRequestPtr req) {
    auto dbClient = drogon::app().getDbClient();

    // Безопасно проверяем, существует ли ключ "user_id" в атрибутах запроса
    if (!req->attributes()->find("user_id")) {
        Json::Value json;
        json["status"] = "error";
        json["message"] = "User ID not found in request context";
        
        auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
        resp->setStatusCode(drogon::k500InternalServerError);
        co_return resp;
    }

    // Извлекаем значение (теперь это абсолютно безопасно)
    std::string userIdStr = req->attributes()->get<std::string>("user_id");

    try {
        auto result = co_await dbClient->execSqlCoro(
            "SELECT id, username, first_name, last_name, user_code, bio, dob FROM users WHERE id = $1;",
            std::stoi(userIdStr)
        );

        if (result.size() == 0) {
            Json::Value json;
            json["status"] = "error";
            json["message"] = "User not found";
            
            auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
            resp->setStatusCode(drogon::k404NotFound);
            co_return resp;
        }

        auto row = result[0];

        Json::Value jsonUser;
        jsonUser["id"] = row["id"].as<int>();
        jsonUser["username"] = row["username"].as<std::string>();
        jsonUser["first_name"] = row["first_name"].as<std::string>();
        jsonUser["last_name"] = row["last_name"].isNull() ? "" : row["last_name"].as<std::string>();
        jsonUser["user_code"] = row["user_code"].as<std::string>();
        jsonUser["bio"] = row["bio"].isNull() ? "" : row["bio"].as<std::string>();
        jsonUser["dob"] = row["dob"].isNull() ? "" : row["dob"].as<std::string>();

        Json::Value json;
        json["status"] = "success";
        json["user"] = jsonUser;

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

drogon::Task<drogon::HttpResponsePtr> UserController::searchUsers(drogon::HttpRequestPtr req) {
    auto dbClient = drogon::app().getDbClient();
    std::string query = req->getParameter("q");

    if (query.empty() || query.length() < 3) {
        Json::Value json;
        json["status"] = "success";
        json["users"] = Json::Value(Json::arrayValue);
        co_return drogon::HttpResponse::newHttpJsonResponse(json);
    }

    // Лямбда-помощник для сборки JSON-ответа (чтобы избежать дублирования кода)
    auto buildJsonResponse = [](const drogon::orm::Result& result) {
        Json::Value jsonUsers(Json::arrayValue);
        for (auto row : result) {
            Json::Value user;
            user["id"] = row["id"].as<int>();
            user["username"] = row["username"].as<std::string>();
            user["first_name"] = row["first_name"].as<std::string>();
            user["last_name"] = row["last_name"].isNull() ? "" : row["last_name"].as<std::string>();
            user["user_code"] = row["user_code"].as<std::string>();
            
            jsonUsers.append(user);
        }

        Json::Value json;
        json["status"] = "success";
        json["users"] = jsonUsers;
        return drogon::HttpResponse::newHttpJsonResponse(json);
    };

    try {
        // Если поиск по уникальному коду пользователя
        if (query.rfind("USR-", 0) == 0) {
            auto result = co_await dbClient->execSqlCoro(
                "SELECT id, username, first_name, last_name, user_code "
                "FROM users WHERE user_code = $1 LIMIT 1;",
                query
            );
            co_return buildJsonResponse(result);
        } 
        // Иначе нечеткий поиск по имени / фамилии / юзернейму
        else {
            std::string likeQuery = "%" + query + "%";
            auto result = co_await dbClient->execSqlCoro(
                "SELECT id, username, first_name, last_name, user_code "
                "FROM users "
                "WHERE username ILIKE $1 OR (first_name || ' ' || last_name) ILIKE $1 "
                "LIMIT 20;",
                likeQuery
            );
            co_return buildJsonResponse(result);
        }

    } catch (const std::exception& e) {
        Json::Value json;
        json["status"] = "error";
        json["message"] = "Database error: " + std::string(e.what());
        
        auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
        resp->setStatusCode(drogon::k500InternalServerError);
        co_return resp;
    }
}