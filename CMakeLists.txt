cmake_minimum_required(VERSION 3.15)
project(messenger_backend CXX)

# Включаем стандарт C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Ищем Drogon и необходимые библиотеки
find_package(Drogon REQUIRED)
find_package(OpenSSL REQUIRED)

# Собираем все .cc файлы из папки src
file(GLOB_RECURSE SRC_FILES "src/*.cc")

# Создаем исполняемый файл
add_executable(${PROJECT_NAME} ${SRC_FILES})

# 1. Линкуем библиотеки как обычно (чтобы CMake подтянул все зависимости типа jsoncpp и libpq)
target_link_libraries(${PROJECT_NAME} PRIVATE
    Drogon::Drogon
    OpenSSL::SSL
    OpenSSL::Crypto
)

# 2. ЖЕСТКО заставляем линкер Linux загрузить ВСЕ скрытые драйверы из libdrogon.a
if(CMAKE_SYSTEM_NAME MATCHES "Linux")
    target_link_options(${PROJECT_NAME} PRIVATE 
        "-Wl,--whole-archive,$<TARGET_FILE:Drogon::Drogon>,--no-whole-archive"
    )
endif()