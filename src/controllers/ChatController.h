#pragma once
#include <drogon/HttpController.h>
#include <drogon/utils/coroutine.h>

class ChatController : public drogon::HttpController<ChatController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(ChatController::createPersonalChat, "/api/v1/chats/personal", drogon::Post, "AuthFilter");
    ADD_METHOD_TO(ChatController::createGroupChat, "/api/v1/chats/group", drogon::Post, "AuthFilter");
    ADD_METHOD_TO(ChatController::getChatList, "/api/v1/chats", drogon::Get, "AuthFilter");
    ADD_METHOD_TO(ChatController::createGroupInvite, "/api/v1/chats/group/{1}/invite", drogon::Post, "AuthFilter");
    ADD_METHOD_TO(ChatController::joinGroupViaInvite, "/api/v1/chats/group/join/{1}", drogon::Post, "AuthFilter");
    
    // ДОБАВЛЕН РОУТ ДЛЯ ИСТОРИИ СООБЩЕНИЙ
    ADD_METHOD_TO(ChatController::getChatMessages, "/api/v1/chats/{1}/messages", drogon::Get, "AuthFilter");
    METHOD_LIST_END

    drogon::Task<drogon::HttpResponsePtr> createPersonalChat(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> createGroupChat(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> getChatList(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> createGroupInvite(drogon::HttpRequestPtr req, std::string chatIdStr);
    drogon::Task<drogon::HttpResponsePtr> joinGroupViaInvite(drogon::HttpRequestPtr req, std::string inviteCode);
    
    // ДОБАВЛЕНО ОБЪЯВЛЕНИЕ МЕТОДА
    drogon::Task<drogon::HttpResponsePtr> getChatMessages(drogon::HttpRequestPtr req, std::string chatIdStr);
};