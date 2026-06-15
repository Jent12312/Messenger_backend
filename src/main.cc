#include <drogon/drogon.h>

int main() {
    LOG_INFO << "Starting Messenger Backend...";
    
    // Загружаем настройки из конфига
    drogon::app().loadConfigFile("config.json");

    // Этот код сработает сразу после успешного старта сервера
    drogon::app().registerBeginningAdvice([]() {
        auto client = drogon::app().getDbClient();
        if (client) {
            LOG_INFO << ">>> SUCCESS: PostgreSQL client initialized and ready!";
        } else {
            LOG_ERROR << ">>> FATAL: PostgreSQL client failed to initialize.";
        }
    });

    // Запускаем цикл (именно здесь создается подключение к БД)
    drogon::app().run();

    return 0;
}