#pragma once
#include <drogon/HttpController.h>
#include <drogon/utils/coroutine.h>

class ChatController : public drogon::HttpController<ChatController> {
public:
    METHOD_LIST_BEGIN
    // Создать или получить личный чат по коду пользователя
    ADD_METHOD_TO(ChatController::createPersonalChat, "/api/v1/chats/personal", drogon::Post, "AuthFilter");
    // Создать новую группу
    ADD_METHOD_TO(ChatController::createGroupChat, "/api/v1/chats/group", drogon::Post, "AuthFilter");
    METHOD_LIST_END

    drogon::Task<drogon::HttpResponsePtr> createPersonalChat(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> createGroupChat(drogon::HttpRequestPtr req);
};