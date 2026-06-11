# Стейдж 1: Сборка
FROM ubuntu:22.04 AS builder

# Установка зависимостей
RUN apt-get update && apt-get install -y \
    g++ cmake make git \
    libjsoncpp-dev uuid-dev zlib1g-dev openssl libssl-dev \
    postgresql-server-dev-all postgresql-client

# Клонируем и устанавливаем Drogon и Trantor
RUN git clone https://github.com/drogonframework/drogon /usr/src/drogon && \
    cd /usr/src/drogon && git submodule update --init && \
    mkdir build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release && \
    make -j$(nproc) && make install

# Копируем наш исходный код
WORKDIR /app
COPY . .

# Собираем наш проект
RUN mkdir build && cd build && cmake .. && make -j$(nproc)

# Стейдж 2: Финальный легковесный образ (Production)
FROM ubuntu:22.04

# Устанавливаем только runtime зависимости (без компиляторов)
RUN apt-get update && apt-get install -y \
    libjsoncpp25 zlib1g openssl libpq5 && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app
# Копируем собранный бинарник из первого стейджа
COPY --from=builder /app/build/messenger_backend /app/messenger_backend

# Открываем порт
EXPOSE 8080

# Запускаем
CMD ["./messenger_backend"]