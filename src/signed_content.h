#pragma once
#include "common.h"
#include "license_public_key.h"
#include <wincrypt.h>
#pragma comment(lib, "crypt32.lib")

namespace tv::signed_content {
inline std::vector<BYTE> decode(const std::string& input) {
    if (input.empty() || input.size() > 24000) return {};
    DWORD count = 0;
    if (!CryptStringToBinaryA(input.data(), static_cast<DWORD>(input.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT,
        nullptr, &count, nullptr, nullptr) || count > 16384) return {};
    std::vector<BYTE> bytes(count);
    if (!CryptStringToBinaryA(input.data(), static_cast<DWORD>(input.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT,
        bytes.data(), &count, nullptr, nullptr)) return {};
    bytes.resize(count); return bytes;
}
inline bool verify(const std::vector<BYTE>& payload, const std::vector<BYTE>& signature) {
    if (payload.empty() || payload.size() > 16384 || signature.size() != 64) return false;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_KEY_HANDLE key = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) < 0) return false;
    std::vector<BYTE> blob(sizeof(BCRYPT_ECCKEY_BLOB) + sizeof(license::kPublicXY));
    const BCRYPT_ECCKEY_BLOB header{BCRYPT_ECDSA_PUBLIC_P256_MAGIC, 32};
    memcpy(blob.data(), &header, sizeof(header));
    memcpy(blob.data() + sizeof(header), license::kPublicXY, sizeof(license::kPublicXY));
    bool ok = false;
    if (BCryptImportKeyPair(algorithm, nullptr, BCRYPT_ECCPUBLIC_BLOB, &key, blob.data(), static_cast<ULONG>(blob.size()), 0) >= 0) {
        BCRYPT_ALG_HANDLE sha = nullptr;
        BYTE digest[32]{};
        if (BCryptOpenAlgorithmProvider(&sha, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0) {
            if (BCryptHash(sha, nullptr, 0, const_cast<PUCHAR>(payload.data()), static_cast<ULONG>(payload.size()), digest, 32) >= 0)
                ok = BCryptVerifySignature(key, nullptr, digest, 32, const_cast<PUCHAR>(signature.data()), 64, 0) >= 0;
            BCryptCloseAlgorithmProvider(sha, 0);
        }
        BCryptDestroyKey(key);
    }
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return ok;
}
}
