#pragma once
#include <drogon/HttpController.h>
#include <drogon/utils/coroutine.h> 

class HealthCheck : public drogon::HttpController<HealthCheck> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(HealthCheck::check, "/api/v1/health", drogon::Get);
    METHOD_LIST_END

    drogon::Task<drogon::HttpResponsePtr> check(drogon::HttpRequestPtr req);
};