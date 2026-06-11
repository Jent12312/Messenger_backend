set(CMAKE_TOOLCHAIN_FILE "B:/Messenger/messenger-backend/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")
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

# Подключаем библиотеки
target_link_libraries(${PROJECT_NAME} PRIVATE
    Drogon::Drogon
    OpenSSL::SSL
    OpenSSL::Crypto
)