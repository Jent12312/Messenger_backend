#include <drogon/drogon.h>

int main() {
    // Настраиваем логгер
    LOG_INFO << "Starting Messenger Backend...";

    // Конфигурируем и запускаем сервер
    drogon::app()
        .setLogPath("")
        .setLogLevel(trantor::Logger::kDebug)
        .addListener("0.0.0.0", 8080)
        .setThreadNum(0) // 0 означает: создать потоки по числу ядер процессора
        .run();

    return 0;
}