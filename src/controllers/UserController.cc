#include "UserController.h"
#include "../utils/CryptoUtils.h"

drogon::Task<drogon::HttpResponsePtr> UserController::getProfile(drogon::HttpRequestPtr req) {
    auto dbClient = drogon::app().getDbClient();

    if (!req->attributes()->find("user_id")) {
        Json::Value json;
        json["status"] = "error";
        json["message"] = "User ID not found in request context";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
        resp->setStatusCode(drogon::k500InternalServerError);
        co_return resp;
    }

    int userId = 0;
    try {
        userId = std::stoi(req->attributes()->get<std::string>("user_id"));
    } catch (const std::exception&) {
        Json::Value json;
        json["status"] = "error";
        json["message"] = "Invalid user ID format";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
        resp->setStatusCode(drogon::k400BadRequest);
        co_return resp;
    }

    try {
        auto result = co_await dbClient->execSqlCoro(
            "SELECT id, username, first_name, last_name, user_code, bio, avatar_url, dob FROM users WHERE id = $1;",
            userId
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
        jsonUser["avatar_url"] = row["avatar_url"].isNull() ? "" : row["avatar_url"].as<std::string>();
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

    std::string escapedQuery = "";
    for (char c : query) {
        if (c == '%' || c == '_') {
            escapedQuery += '\\';
        }
        escapedQuery += c;
    }

    auto buildJsonResponse = [](const drogon::orm::Result& result) {
        Json::Value jsonUsers(Json::arrayValue);
        for (auto row : result) {
            Json::Value user;
            user["id"] = row["id"].as<int>();
            user["username"] = row["username"].as<std::string>();
            user["first_name"] = row["first_name"].as<std::string>();
            user["last_name"] = row["last_name"].isNull() ? "" : row["last_name"].as<std::string>();
            user["user_code"] = row["user_code"].as<std::string>();
            user["avatar_url"] = row["avatar_url"].isNull() ? "" : row["avatar_url"].as<std::string>();
            jsonUsers.append(user);
        }

        Json::Value json;
        json["status"] = "success";
        json["users"] = jsonUsers;
        return drogon::HttpResponse::newHttpJsonResponse(json);
    };

    try {
        if (escapedQuery.rfind("USR-", 0) == 0) {
            auto result = co_await dbClient->execSqlCoro(
                "SELECT id, username, first_name, last_name, user_code, avatar_url "
                "FROM users WHERE user_code = $1 LIMIT 1;",
                escapedQuery
            );
            co_return buildJsonResponse(result);
        } else {
            std::string likeQuery = "%" + escapedQuery + "%";
            auto result = co_await dbClient->execSqlCoro(
                "SELECT id, username, first_name, last_name, user_code, avatar_url "
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

// 1. РЕДАКТИРОВАНИЕ ТЕКСТОВОГО ПРОФИЛЯ
drogon::Task<drogon::HttpResponsePtr> UserController::updateProfile(drogon::HttpRequestPtr req) {
    auto dbClient = drogon::app().getDbClient();
    int userId = std::stoi(req->attributes()->get<std::string>("user_id"));
    auto jsonBody = req->getJsonObject();

    if (!jsonBody) {
        Json::Value json;
        json["status"] = "error";
        json["message"] = "Missing JSON body";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
        resp->setStatusCode(drogon::k400BadRequest);
        co_return resp;
    }

    std::string firstName = (*jsonBody)["first_name"] ? (*jsonBody)["first_name"].asString() : "";
    std::string lastName = (*jsonBody)["last_name"] ? (*jsonBody)["last_name"].asString() : "";
    std::string bio = (*jsonBody)["bio"] ? (*jsonBody)["bio"].asString() : "";
    std::string dob = (*jsonBody)["dob"] ? (*jsonBody)["dob"].asString() : "";

    if (firstName.empty()) {
        Json::Value json;
        json["status"] = "error";
        json["message"] = "first_name cannot be empty";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
        resp->setStatusCode(drogon::k400BadRequest);
        co_return resp;
    }

    try {
        if (dob.empty()) {
            co_await dbClient->execSqlCoro(
                "UPDATE users SET first_name = $1, last_name = $2, bio = $3 WHERE id = $4;",
                firstName, lastName, bio, userId
            );
        } else {
            co_await dbClient->execSqlCoro(
                "UPDATE users SET first_name = $1, last_name = $2, bio = $3, dob = $4::date WHERE id = $5;",
                firstName, lastName, bio, dob, userId
            );
        }

        Json::Value json;
        json["status"] = "success";
        json["message"] = "Profile updated successfully";

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

// 2. ЗАГРУЗКА АВАТАРКИ (MULTIPART/FORM-DATA С ПОТОКОВЫМ СОХРАНЕНИЕМ)
drogon::Task<drogon::HttpResponsePtr> UserController::uploadAvatar(drogon::HttpRequestPtr req) {
    auto dbClient = drogon::app().getDbClient();
    int userId = std::stoi(req->attributes()->get<std::string>("user_id"));

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
    
    // Валидация расширения картинки
    if (ext != "jpg" && ext != "jpeg" && ext != "png" && ext != "webp") {
        Json::Value json;
        json["status"] = "error";
        json["message"] = "Invalid image format. Allowed: jpg, jpeg, png, webp";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
        resp->setStatusCode(drogon::k400BadRequest);
        co_return resp;
    }

    // Генерируем уникальное имя файла
    std::string newFileName = "avatar_" + std::to_string(userId) + "_" + CryptoUtils::generateSessionToken().substr(0, 8) + "." + ext;
    
    // Сохраняем файл в папку ./uploads/avatars/
    file.saveAs("avatars/" + newFileName);

    // Формируем публичную ссылку
    std::string avatarUrl = "https://aegischat.jents.online/uploads/avatars/" + newFileName;

    try {
        co_await dbClient->execSqlCoro(
            "UPDATE users SET avatar_url = $1 WHERE id = $2;",
            avatarUrl, userId
        );

        Json::Value json;
        json["status"] = "success";
        json["avatar_url"] = avatarUrl;
        json["message"] = "Avatar uploaded successfully";

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