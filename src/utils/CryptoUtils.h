#pragma once
#include <string>
#include <iomanip>
#include <sstream>
#include <random>
#include <openssl/sha.h>

class CryptoUtils {
public:
    // Хэширование пароля SHA-256
    static std::string hashPassword(const std::string& password) {
        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256_CTX sha256;
        SHA256_Init(&sha256);
        SHA256_Update(&sha256, password.c_str(), password.size());
        SHA256_Final(hash, &sha256);

        std::stringstream ss;
        for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
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
        
        return code; // Пример: USR-K9F2-RT8A
    }
};