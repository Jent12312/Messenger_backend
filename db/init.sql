-- ПОДКЛЮЧЕНИЕ РАСШИРЕНИЙ
CREATE EXTENSION IF NOT EXISTS pg_trgm;

-- ТАБЛИЦА ПОЛЬЗОВАТЕЛЕЙ
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

CREATE INDEX IF NOT EXISTS idx_users_username ON users(username);
CREATE INDEX IF NOT EXISTS idx_users_user_code ON users(user_code);
CREATE INDEX IF NOT EXISTS idx_users_username_trgm ON users USING GIN (username gin_trgm_ops);
CREATE INDEX IF NOT EXISTS idx_users_names_trgm ON users USING GIN ((first_name || ' ' || last_name) gin_trgm_ops);

-- ТАБЛИЦА ЧАТОВ
CREATE TABLE IF NOT EXISTS chats (
    id SERIAL PRIMARY KEY,
    type VARCHAR(10) NOT NULL,
    title VARCHAR(100),
    description VARCHAR(255) DEFAULT '',
    avatar_url VARCHAR(255) DEFAULT '',
    created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP
);

-- ТАБЛИЦА УЧАСТНИКОВ ЧАТОВ
CREATE TABLE IF NOT EXISTS chat_members (
    chat_id INT REFERENCES chats(id) ON DELETE CASCADE,
    user_id INT REFERENCES users(id) ON DELETE CASCADE,
    role VARCHAR(20) DEFAULT 'member',
    is_pinned BOOLEAN DEFAULT FALSE,
    last_read_message_id INT DEFAULT 0,
    PRIMARY KEY (chat_id, user_id)
);

CREATE INDEX IF NOT EXISTS idx_chat_members_user_id ON chat_members(user_id);

-- ТАБЛИЦА СООБЩЕНИЙ
CREATE TABLE IF NOT EXISTS messages (
    id SERIAL PRIMARY KEY,
    chat_id INT REFERENCES chats(id) ON DELETE CASCADE,
    sender_id INT REFERENCES users(id) ON DELETE SET NULL,
    text TEXT NOT NULL DEFAULT '',
    type VARCHAR(20) DEFAULT 'text',
    file_url VARCHAR(255) DEFAULT '',
    is_read BOOLEAN DEFAULT FALSE,
    is_edited BOOLEAN DEFAULT FALSE,
    created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_messages_chat_id ON messages(chat_id);
CREATE INDEX IF NOT EXISTS idx_messages_chat_id_id ON messages(chat_id, id DESC);

-- ТАБЛИЦА РЕАКЦИЙ
CREATE TABLE IF NOT EXISTS message_reactions (
    message_id INT REFERENCES messages(id) ON DELETE CASCADE,
    user_id INT REFERENCES users(id) ON DELETE CASCADE,
    emoji VARCHAR(16) NOT NULL,
    created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (message_id, user_id)
);

CREATE INDEX IF NOT EXISTS idx_reactions_message_id ON message_reactions(message_id);

-- ТАБЛИЦА ИНВАЙТОВ
CREATE TABLE IF NOT EXISTS chat_invites (
    id SERIAL PRIMARY KEY,
    chat_id INT REFERENCES chats(id) ON DELETE CASCADE,
    invite_code VARCHAR(32) UNIQUE NOT NULL,
    created_by INT REFERENCES users(id) ON DELETE CASCADE,
    expires_at TIMESTAMP WITH TIME ZONE,
    created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_chat_invites_code ON chat_invites(invite_code);

-- ТАБЛИЦА БАН-ЛИСТОВ
CREATE TABLE IF NOT EXISTS chat_banned_users (
    chat_id INT REFERENCES chats(id) ON DELETE CASCADE,
    user_id INT REFERENCES users(id) ON DELETE CASCADE,
    banned_by INT REFERENCES users(id) ON DELETE SET NULL,
    reason VARCHAR(255) DEFAULT '',
    created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (chat_id, user_id)
);