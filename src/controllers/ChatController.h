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
    ADD_METHOD_TO(ChatController::getChatMessages, "/api/v1/chats/{1}/messages", drogon::Get, "AuthFilter");
    ADD_METHOD_TO(ChatController::togglePinChat, "/api/v1/chats/{1}/pin", drogon::Put, "AuthFilter");
    ADD_METHOD_TO(ChatController::uploadChatFile, "/api/v1/chats/{1}/files", drogon::Post, "AuthFilter");
    ADD_METHOD_TO(ChatController::uploadGroupAvatar, "/api/v1/chats/group/{1}/avatar", drogon::Post, "AuthFilter");
    ADD_METHOD_TO(ChatController::updateMemberRole, "/api/v1/chats/group/{1}/members/{2}/role", drogon::Put, "AuthFilter");
    ADD_METHOD_TO(ChatController::removeMember, "/api/v1/chats/group/{1}/members/{2}", drogon::Delete, "AuthFilter");

    // НОВЫЕ МЕТОДЫ ДЛЯ ГРУПП И БАН-ЛИСТОВ
    ADD_METHOD_TO(ChatController::getGroupDetails, "/api/v1/chats/group/{1}", drogon::Get, "AuthFilter");
    ADD_METHOD_TO(ChatController::updateGroupInfo, "/api/v1/chats/group/{1}", drogon::Put, "AuthFilter");
    ADD_METHOD_TO(ChatController::banMember, "/api/v1/chats/group/{1}/ban/{2}", drogon::Post, "AuthFilter");
    ADD_METHOD_TO(ChatController::unbanMember, "/api/v1/chats/group/{1}/ban/{2}", drogon::Delete, "AuthFilter");
    METHOD_LIST_END

    drogon::Task<drogon::HttpResponsePtr> createPersonalChat(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> createGroupChat(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> getChatList(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> createGroupInvite(drogon::HttpRequestPtr req, std::string chatIdStr);
    drogon::Task<drogon::HttpResponsePtr> joinGroupViaInvite(drogon::HttpRequestPtr req, std::string inviteCode);
    drogon::Task<drogon::HttpResponsePtr> getChatMessages(drogon::HttpRequestPtr req, std::string chatIdStr);
    drogon::Task<drogon::HttpResponsePtr> togglePinChat(drogon::HttpRequestPtr req, std::string chatIdStr);
    drogon::Task<drogon::HttpResponsePtr> uploadChatFile(drogon::HttpRequestPtr req, std::string chatIdStr);
    drogon::Task<drogon::HttpResponsePtr> uploadGroupAvatar(drogon::HttpRequestPtr req, std::string chatIdStr);
    drogon::Task<drogon::HttpResponsePtr> updateMemberRole(drogon::HttpRequestPtr req, std::string chatIdStr, std::string targetUserIdStr);
    drogon::Task<drogon::HttpResponsePtr> removeMember(drogon::HttpRequestPtr req, std::string chatIdStr, std::string targetUserIdStr);

    drogon::Task<drogon::HttpResponsePtr> getGroupDetails(drogon::HttpRequestPtr req, std::string chatIdStr);
    drogon::Task<drogon::HttpResponsePtr> updateGroupInfo(drogon::HttpRequestPtr req, std::string chatIdStr);
    drogon::Task<drogon::HttpResponsePtr> banMember(drogon::HttpRequestPtr req, std::string chatIdStr, std::string targetUserIdStr);
    drogon::Task<drogon::HttpResponsePtr> unbanMember(drogon::HttpRequestPtr req, std::string chatIdStr, std::string targetUserIdStr);
};