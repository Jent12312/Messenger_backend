#pragma once
#include <drogon/HttpFilter.h>

class AuthFilter : public drogon::HttpFilter<AuthFilter> {
public:
    // Главный метод фильтрации
    void doFilter(const drogon::HttpRequestPtr &req,
                  drogon::FilterCallback &&fcb,
                  drogon::FilterChainCallback &&fccb) override;
};