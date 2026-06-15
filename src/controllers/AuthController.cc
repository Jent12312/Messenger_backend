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

drogon::Task<drogon::HttpResponsePtr> AuthController::loginUser(drogon::HttpRequestPtr req) {
    auto dbClient = drogon::app().getDbClient();
    auto redisClient = drogon::app().getRedisClient();
    auto jsonBody = req->getJsonObject();

    if (!jsonBody || !(*jsonBody)["username"] || !(*jsonBody)["password"]) {
        Json::Value json;
        json["status"] = "error";
        json["message"] = "Missing username or password";
        
        auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
        resp->setStatusCode(drogon::k400BadRequest);
        co_return resp;
    }

    std::string username = (*jsonBody)["username"].asString();
    std::string password = (*jsonBody)["password"].asString();

    try {
        // 1. Ищем пользователя в PostgreSQL
        auto result = co_await dbClient->execSqlCoro(
            "SELECT id, password_hash, first_name, last_name, user_code FROM users WHERE username = $1;", 
            username
        );

        if (result.size() == 0) {
            Json::Value json;
            json["status"] = "error";
            json["message"] = "Invalid username or password";
            
            auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
            resp->setStatusCode(drogon::k401Unauthorized);
            co_return resp;
        }

        auto row = result[0];
        std::string dbPasswordHash = row["password_hash"].as<std::string>();

        // 2. Сверяем хэши паролей
        if (CryptoUtils::hashPassword(password) != dbPasswordHash) {
            Json::Value json;
            json["status"] = "error";
            json["message"] = "Invalid username or password";
            
            auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
            resp->setStatusCode(drogon::k401Unauthorized);
            co_return resp;
        }

        int userId = row["id"].as<int>();
        std::string userCode = row["user_code"].as<std::string>();
        std::string firstName = row["first_name"].as<std::string>();
        std::string lastName = row["last_name"].as<std::string>();

        // 3. Генерируем токен сессии
        std::string token = CryptoUtils::generateSessionToken();

        // 4. Записываем сессию в Redis асинхронно
        // Ключ: "session:<token>", Значение: ID пользователя, Срок действия: 2592000 секунд (30 дней)
        std::string redisKey = "session:" + token;
        co_await redisClient->execCommandCoro(
            "SET %s %s EX %d", 
            redisKey.c_str(), 
            std::to_string(userId).c_str(), 
            2592000
        );

        // 5. Возвращаем успешный ответ клиенту с токеном и профилем пользователя
        Json::Value json;
        json["status"] = "success";
        json["token"] = token;
        json["user"]["id"] = userId;
        json["user"]["username"] = username;
        json["user"]["user_code"] = userCode;
        json["user"]["first_name"] = firstName;
        json["user"]["last_name"] = lastName;

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