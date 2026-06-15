#pragma once
#include <string>
#include <iomanip>
#include <sstream>
#include <random>
#include <openssl/evp.h> // Используем новый EVP интерфейс OpenSSL

class CryptoUtils {
public:
    // Хэширование пароля SHA-256 через современный EVP API
    static std::string hashPassword(const std::string& password) {
        EVP_MD_CTX* context = EVP_MD_CTX_new();
        const EVP_MD* md = EVP_sha256();
        unsigned char hash[EVP_MAX_MD_SIZE];
        unsigned int hashLen = 0;

        EVP_DigestInit_ex(context, md, nullptr);
        EVP_DigestUpdate(context, password.c_str(), password.size());
        EVP_DigestFinal_ex(context, hash, &hashLen);
        EVP_MD_CTX_free(context);

        std::stringstream ss;
        for (unsigned int i = 0; i < hashLen; i++) {
            ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
        }
        return ss.str();
    }

    // Генерация случайного кода формата USER-XXXX-XXXX
    static std::string generateUserCode() {
        const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        std::random_device rd;
        std::mt19937 generator(rd());
        std::uniform_int_distribution<> distribution(0, chars.size() - 1);

        std::string code = "USR-";
        for (int i = 0; i < 4; ++i) code += chars[distribution(generator)];
        code += "-";
        for (int i = 0; i < 4; ++i) code += chars[distribution(generator)];
        
        return code;
    }

    // Генерация случайного токена сессии (64 символа)
    static std::string generateSessionToken() {
        const std::string chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        std::random_device rd;
        std::mt19937 generator(rd());
        std::uniform_int_distribution<> distribution(0, chars.size() - 1);

        std::string token = "";
        for (int i = 0; i < 64; ++i) {
            token += chars[distribution(generator)];
        }
        return token;
    }
};