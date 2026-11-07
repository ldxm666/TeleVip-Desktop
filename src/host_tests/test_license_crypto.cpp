// Include the production licensing implementation so the production CNG verification path is exercised.
#include "../license.cpp"
#include "license_signature_fixture.h"
#include <cassert>
#include <iostream>

int main() {
    using namespace tv::license;
    const std::string text = "TeleVip verification fixture v1";
    std::vector<BYTE> payload(text.begin(), text.end());
    std::vector<BYTE> signature(std::begin(kFixtureSignature), std::end(kFixtureSignature));
    assert(signature_valid(payload, signature));
    payload[0] ^= 1;
    assert(!signature_valid(payload, signature));
    payload[0] ^= 1;
    signature[7] ^= 1;
    assert(!signature_valid(payload, signature));
    signature.resize(63);
    assert(!signature_valid(payload, signature));
    assert(!signature_valid({}, {}));
    assert(unbase64("%%%invalid%%%").empty());
    assert(!is_hex("../../../hook.dll"));
    assert(is_hex(std::string(64, 'a')));
    deadline = 0;
    assert(!authorized() && hook_path().empty());
    std::cout << "license crypto: real CNG signature, tampered payload/signature, invalid encoding and fail-closed expiry passed\n";
}
