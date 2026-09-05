#include "AuthController.h"
#include "../utils/CryptoUtils.h"

drogon::Task<drogon::HttpResponsePtr> AuthController::registerUser(drogon::HttpRequestPtr req) {
    auto dbClient = drogon::app().getDbClient();
    auto jsonBody = req->getJsonObject();

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

    // 1. Валидация username (длина 3-30 символов, только a-z, A-Z, 0-9 и _)
    if (username.length() < 3 || username.length() > 30) {
        Json::Value json;
        json["status"] = "error";
        json["message"] = "Username must be between 3 and 30 characters long";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
        resp->setStatusCode(drogon::k400BadRequest);
        co_return resp;
    }

    for (char c : username) {
        if (!isalnum(c) && c != '_') {
            Json::Value json;
            json["status"] = "error";
            json["message"] = "Username can only contain letters, numbers, and underscores";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
            resp->setStatusCode(drogon::k400BadRequest);
            co_return resp;
        }
    }

    // 2. Валидация сложности пароля (минимум 8 символов)
    if (password.length() < 8) {
        Json::Value json;
        json["status"] = "error";
        json["message"] = "Password must be at least 8 characters long";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
        resp->setStatusCode(drogon::k400BadRequest);
        co_return resp;
    }

    try {
        auto checkUser = co_await dbClient->execSqlCoro("SELECT id FROM users WHERE username = $1;", username);
        if (checkUser.size() > 0) {
            Json::Value json;
            json["status"] = "error";
            json["message"] = "Username is already taken";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
            resp->setStatusCode(drogon::k400BadRequest);
            co_return resp;
        }

        std::string passwordHash = CryptoUtils::hashPassword(password);
        std::string userCode = CryptoUtils::generateUserCode();

        co_await dbClient->execSqlCoro(
            "INSERT INTO users (username, password_hash, first_name, last_name, user_code) VALUES ($1, $2, $3, $4, $5);",
            username, passwordHash, firstName, lastName, userCode
        );

        Json::Value json;
        json["status"] = "success";
        json["message"] = "User registered successfully";
        json["user_code"] = userCode;

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
        std::string lastName = row["last_name"].isNull() ? "" : row["last_name"].as<std::string>();

        std::string token = CryptoUtils::generateSessionToken();

        std::string redisKey = "session:" + token;
        co_await redisClient->execCommandCoro(
            "SET %s %s EX %d", 
            redisKey.c_str(), 
            std::to_string(userId).c_str(), 
            2592000 // 30 дней
        );

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