#pragma once
#include <drogon/HttpController.h>
#include <drogon/utils/coroutine.h>

class UserController : public drogon::HttpController<UserController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(UserController::getProfile, "/api/v1/user/profile", drogon::Get, "AuthFilter");
    ADD_METHOD_TO(UserController::searchUsers, "/api/v1/user/search", drogon::Get, "AuthFilter");
    
    // РЕДАКТИРОВАНИЕ ПРОФИЛЯ И АВАТАРА
    ADD_METHOD_TO(UserController::updateProfile, "/api/v1/user/profile", drogon::Put, "AuthFilter");
    ADD_METHOD_TO(UserController::uploadAvatar, "/api/v1/user/avatar", drogon::Post, "AuthFilter");
    METHOD_LIST_END

    drogon::Task<drogon::HttpResponsePtr> getProfile(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> searchUsers(drogon::HttpRequestPtr req);
    
    drogon::Task<drogon::HttpResponsePtr> updateProfile(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> uploadAvatar(drogon::HttpRequestPtr req);
};