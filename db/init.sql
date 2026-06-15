CREATE TABLE IF NOT EXISTS users (
    id SERIAL PRIMARY KEY,
    username VARCHAR(50) UNIQUE NOT NULL,
    password_hash VARCHAR(255) NOT NULL,
    first_name VARCHAR(50) NOT NULL,
    last_name VARCHAR(50),
    user_code VARCHAR(20) UNIQUE NOT NULL, -- Тот самый уникальный код пользователя
    bio VARCHAR(255) DEFAULT '',
    dob DATE,
    created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP
);

-- Создаем индексы для быстрого поиска
CREATE INDEX IF NOT EXISTS idx_users_username ON users(username);
CREATE INDEX IF NOT EXISTS idx_users_user_code ON users(user_code);

-- Включаем расширение для нечеткого поиска (триграммы)
CREATE EXTENSION IF NOT EXISTS pg_trgm;

-- Создаем GIN-индексы для мгновенного поиска по тексту
CREATE INDEX IF NOT EXISTS idx_users_username_trgm ON users USING GIN (username gin_trgm_ops);
CREATE INDEX IF NOT EXISTS idx_users_names_trgm ON users USING GIN ((first_name || ' ' || last_name) gin_trgm_ops);