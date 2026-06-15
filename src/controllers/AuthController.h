#pragma once
#include <drogon/HttpController.h>
#include <drogon/utils/coroutine.h>

class AuthController : public drogon::HttpController<AuthController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(AuthController::registerUser, "/api/v1/auth/register", drogon::Post);
    METHOD_LIST_END

    drogon::Task<drogon::HttpResponsePtr> registerUser(drogon::HttpRequestPtr req);
};