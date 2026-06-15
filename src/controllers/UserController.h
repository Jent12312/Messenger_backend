#pragma once
#include <drogon/HttpController.h>
#include <drogon/utils/coroutine.h>

class UserController : public drogon::HttpController<UserController> {
public:
    METHOD_LIST_BEGIN
    // Четвертый параметр в макросе указывает, что к этому эндпоинту применяется AuthFilter
    ADD_METHOD_TO(UserController::getProfile, "/api/v1/user/profile", drogon::Get, "AuthFilter");
    METHOD_LIST_END

    drogon::Task<drogon::HttpResponsePtr> getProfile(drogon::HttpRequestPtr req);
};