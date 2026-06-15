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