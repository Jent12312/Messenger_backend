#pragma once
#include <string>
#include <iomanip>
#include <sstream>
#include <random>
#include <openssl/evp.h> // Используем новый EVP интерфейс OpenSSL
using namespace std;

class CryptoUtils {
public:
    // Хэширование пароля SHA-256 через современный EVP API
    static string hashPassword(const string& password) {
        EVP_MD_CTX* context = EVP_MD_CTX_new();
        const EVP_MD* md = EVP_sha256();
        unsigned char hash[EVP_MAX_MD_SIZE];
        unsigned int hashLen = 0;

        EVP_DigestInit_ex(context, md, nullptr);
        EVP_DigestUpdate(context, password.c_str(), password.size());
        EVP_DigestFinal_ex(context, hash, &hashLen);
        EVP_MD_CTX_free(context);

        stringstream ss;
        for (unsigned int i = 0; i < hashLen; i++) {
            ss << hex << setw(2) << setfill('0') << (int)hash[i];
        }
        return ss.str();
    }

    // Генерация случайного кода формата USER-XXXX-XXXX
    static string generateUserCode() {
        const string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        random_device rd;
        mt19937 generator(rd());
        uniform_int_distribution<> distribution(0, chars.size() - 1);

        string code = "USR-";
        for (int i = 0; i < 4; ++i) code += chars[distribution(generator)];
        code += "-";
        for (int i = 0; i < 4; ++i) code += chars[distribution(generator)];
        
        return code;
    }

    // Генерация случайного токена сессии (64 символа)
    static string generateSessionToken() {
        const string chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        random_device rd;
        mt19937 generator(rd());
        uniform_int_distribution<> distribution(0, chars.size() - 1);

        string token = "";
        for (int i = 0; i < 64; ++i) {
            token += chars[distribution(generator)];
        }
        return token;
    }
};