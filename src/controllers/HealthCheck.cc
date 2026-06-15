#include "HealthCheck.h"

drogon::Task<drogon::HttpResponsePtr> HealthCheck::check(drogon::HttpRequestPtr req) {
    auto dbClient = drogon::app().getDbClient();
    
    // Защита, если клиент БД не инициализирован
    if (!dbClient) {
        Json::Value json;
        json["status"] = "error";
        json["message"] = "Database client is not initialized";
        
        auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
        resp->setStatusCode(drogon::k500InternalServerError);
        co_return resp;
    }

    try {
        // Выполняем асинхронный запрос через сорутину
        auto result = co_await dbClient->execSqlCoro("SELECT 1 AS status;");
        
        Json::Value json;
        json["status"] = "ok";
        json["db_connection"] = "success";
        
        co_return drogon::HttpResponse::newHttpJsonResponse(json);
    } catch (const drogon::orm::DrogonDbException& e) {
        Json::Value json;
        json["status"] = "error";
        json["message"] = e.base().what();
        
        auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
        resp->setStatusCode(drogon::k500InternalServerError);
        co_return resp;
    }
}