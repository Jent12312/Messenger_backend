#include "AuthController.h"
#include "../utils/CryptoUtils.h"

drogon::Task<drogon::HttpResponsePtr> AuthController::registerUser(drogon::HttpRequestPtr req) {
    auto dbClient = drogon::app().getDbClient();
    auto jsonBody = req->getJsonObject();

    // Проверка на валидность JSON
    if (!jsonBody || !(*jsonBody)["username"] || !(*jsonBody)["password"] || !(*jsonBody)["first_name"]) {
        Json::Value json;
        json["status"] = "error";
        json["message"] = "Missing required fields (username, password, first_name)";
        
        auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
        resp->setStatusCode(drogon::k400BadRequest);
        co_return resp;
    }

    std::string username = (*jsonBody)["username"].asString();
    std::string password = (*jsonBody)["password"].asString();
    std::string firstName = (*jsonBody)["first_name"].asString();
    std::string lastName = (*jsonBody)["last_name"] ? (*jsonBody)["last_name"].asString() : "";

    try {
        // 1. Проверяем, существует ли уже пользователь с таким username
        auto checkUser = co_await dbClient->execSqlCoro("SELECT id FROM users WHERE username = $1;", username);
        if (checkUser.size() > 0) {
            Json::Value json;
            json["status"] = "error";
            json["message"] = "Username is already taken";
            
            auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
            resp->setStatusCode(drogon::k400BadRequest);
            co_return resp;
        }

        // 2. Хэшируем пароль и генерируем уникальный user_code
        std::string passwordHash = CryptoUtils::hashPassword(password);
        std::string userCode = CryptoUtils::generateUserCode();

        // 3. Сохраняем в базу данных
        co_await dbClient->execSqlCoro(
            "INSERT INTO users (username, password_hash, first_name, last_name, user_code) VALUES ($1, $2, $3, $4, $5);",
            username, passwordHash, firstName, lastName, userCode
        );

        // 4. Отправляем успешный ответ
        Json::Value json;
        json["status"] = "success";
        json["message"] = "User registered successfully";
        json["user_code"] = userCode; // Возвращаем сгенерированный код

        co_return drogon::HttpResponse::newHttpJsonResponse(json);

    } catch (const drogon::orm::DrogonDbException& e) {
        Json::Value json;
        json["status"] = "error";
        json["message"] = "Database error: " + std::string(e.base().what());
        
        auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
        resp->setStatusCode(drogon::k500InternalServerError);
        co_return resp;
    }
}