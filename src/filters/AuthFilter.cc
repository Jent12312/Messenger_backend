#include "AuthFilter.h"
#include <drogon/drogon.h>

void AuthFilter::doFilter(const drogon::HttpRequestPtr &req,
                          drogon::FilterCallback &&fcb,
                          drogon::FilterChainCallback &&fccb) {
    
    std::string authHeader = req->getHeader("Authorization");

    if (authHeader.empty() || authHeader.rfind("Bearer ", 0) != 0) {
        Json::Value json;
        json["status"] = "error";
        json["message"] = "Unauthorized: Missing or invalid token format";
        
        auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
        resp->setStatusCode(drogon::k401Unauthorized);
        fcb(resp);
        return;
    }

    std::string token = authHeader.substr(7);
    auto redisClient = drogon::app().getRedisClient();
    std::string redisKey = "session:" + token;

    // Используем правильные типы из drogon::nosql
    redisClient->execCommandAsync(
        [fcb, fccb, req, redisKey, token, redisClient](const drogon::nosql::RedisResult &r) {
            if (r.type() == drogon::nosql::RedisResultType::kNil) {
                Json::Value json;
                json["status"] = "error";
                json["message"] = "Unauthorized: Session expired or invalid";
                
                auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
                resp->setStatusCode(drogon::k401Unauthorized);
                fcb(resp);
                return;
            }

            std::string userIdStr = r.asString();
            req->attributes()->insert("user_id", userIdStr);

            // Продлеваем сессию на 30 дней
            redisClient->execCommandAsync(
                [](const drogon::nosql::RedisResult &){},
                [](const drogon::nosql::RedisException &){},
                "EXPIRE %s 2592000", redisKey.c_str()
            );

            fccb();
        },
        [fcb](const drogon::nosql::RedisException &e) {
            Json::Value json;
            json["status"] = "error";
            json["message"] = "Internal Redis error: " + std::string(e.what());
            
            auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
            resp->setStatusCode(drogon::k500InternalServerError);
            fcb(resp);
        },
        "GET %s", redisKey.c_str()
    );
}