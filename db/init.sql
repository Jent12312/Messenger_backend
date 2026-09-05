-- ==========================================================
-- 1. РАСШИРЕНИЯ POSTGRESQL
-- ==========================================================
-- Включаем триграммы для мгновенного нечеткого поиска пользователей
CREATE EXTENSION IF NOT EXISTS pg_trgm;

-- ==========================================================
-- 2. ТАБЛИЦА ПОЛЬЗОВАТЕЛЕЙ (USERS)
-- ==========================================================
CREATE TABLE IF NOT EXISTS users (
    id SERIAL PRIMARY KEY,
    username VARCHAR(50) UNIQUE NOT NULL,
    password_hash VARCHAR(255) NOT NULL,
    first_name VARCHAR(50) NOT NULL,
    last_name VARCHAR(50),
    user_code VARCHAR(32) UNIQUE NOT NULL,
    bio VARCHAR(255) DEFAULT '',
    avatar_url VARCHAR(255) DEFAULT '',
    dob DATE,
    last_seen TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
    created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP
);

-- Индексы пользователей
CREATE INDEX IF NOT EXISTS idx_users_username ON users(username);
CREATE INDEX IF NOT EXISTS idx_users_user_code ON users(user_code);
CREATE INDEX IF NOT EXISTS idx_users_username_trgm ON users USING GIN (username gin_trgm_ops);
CREATE INDEX IF NOT EXISTS idx_users_names_trgm ON users USING GIN ((first_name || ' ' || COALESCE(last_name, '')) gin_trgm_ops);

-- ==========================================================
-- 3. ТАБЛИЦА ЧАТОВ И ГРУПП (CHATS)
-- ==========================================================
CREATE TABLE IF NOT EXISTS chats (
    id SERIAL PRIMARY KEY,
    type VARCHAR(10) NOT NULL, -- 'personal' или 'group'
    title VARCHAR(100),        -- Название (только для групп)
    description TEXT DEFAULT '', -- Описание группы
    avatar_url VARCHAR(255),   -- Аватарка группы
    created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP
);

-- ==========================================================
-- 4. ТАБЛИЦА УЧАСТНИКОВ ЧАТОВ (CHAT_MEMBERS)
-- ==========================================================
CREATE TABLE IF NOT EXISTS chat_members (
    chat_id INT REFERENCES chats(id) ON DELETE CASCADE,
    user_id INT REFERENCES users(id) ON DELETE CASCADE,
    role VARCHAR(20) DEFAULT 'member', -- 'member', 'admin', 'creator'
    is_pinned BOOLEAN DEFAULT FALSE,
    last_read_message_id INT DEFAULT 0,
    PRIMARY KEY (chat_id, user_id)
);

-- ==========================================================
-- 5. ТАБЛИЦА ИСТОРИИ СООБЩЕНИЙ (MESSAGES)
-- ==========================================================
CREATE TABLE IF NOT EXISTS messages (
    id SERIAL PRIMARY KEY,
    chat_id INT REFERENCES chats(id) ON DELETE CASCADE,
    sender_id INT REFERENCES users(id) ON DELETE SET NULL,
    text TEXT NOT NULL,
    type VARCHAR(20) DEFAULT 'text', -- 'text', 'image', 'video', 'audio', 'file'
    file_url VARCHAR(255),
    is_read BOOLEAN DEFAULT FALSE,
    is_edited BOOLEAN DEFAULT FALSE,
    created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_messages_chat_id ON messages(chat_id);

-- ==========================================================
-- 6. ТАБЛИЦА РЕАКЦИЙ НА СООБЩЕНИЯ (MESSAGE_REACTIONS)
-- ==========================================================
CREATE TABLE IF NOT EXISTS message_reactions (
    message_id INT REFERENCES messages(id) ON DELETE CASCADE,
    user_id INT REFERENCES users(id) ON DELETE CASCADE,
    emoji VARCHAR(16) NOT NULL,
    created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (message_id, user_id)
);

-- ==========================================================
-- 7. ТАБЛИЦА ССЫЛОК-ПРИГЛАШЕНИЙ В ГРУППЫ (CHAT_INVITES)
-- ==========================================================
CREATE TABLE IF NOT EXISTS chat_invites (
    id SERIAL PRIMARY KEY,
    chat_id INT REFERENCES chats(id) ON DELETE CASCADE,
    invite_code VARCHAR(32) UNIQUE NOT NULL,
    created_by INT REFERENCES users(id) ON DELETE CASCADE,
    expires_at TIMESTAMP WITH TIME ZONE,
    created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP
);