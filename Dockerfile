# Используем официальный образ от создателей Drogon (в нем уже есть всё: C++, PostgreSQL драйверы, CMake)
FROM drogonframework/drogon:latest

# Устанавливаем рабочую директорию
WORKDIR /app

# Копируем весь наш код в контейнер
COPY . .

# Собираем только наш код (сам фреймворк уже собран)
RUN mkdir -p build && cd build && \
    cmake .. && make -j$(nproc)

# Копируем конфиг в папку со скомпилированным бинарником
RUN cp config.json build/config.json

# Открываем порт
EXPOSE 8080

# Указываем рабочую папку для запуска и команду
WORKDIR /app/build
RUN mkdir -p uploads/avatars uploads/files
CMD ["./messenger_backend"]