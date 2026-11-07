#include "json_text.h"

#include <cassert>
#include <iostream>

int main() {
    assert(tv::json_string_member(R"({"responseData":{"translatedText":"Hello \"world\"\nC:\\Temp"},"responseStatus":200})", "translatedText") ==
           "Hello \"world\"\nC:\\Temp");
    assert(tv::json_string_member(R"({"text":"\uD83D\uDE00 \u4F60\u597D"})", "text") ==
           "\xF0\x9F\x98\x80 \xE4\xBD\xA0\xE5\xA5\xBD");
    assert(tv::json_string_member(R"({"text":"bad\uZZZZ"})", "text").empty());
    assert(tv::json_string_member(R"({"text":"bad\uD83D"})", "text").empty());
    assert(tv::json_string_member(R"({"text":"bad\uDE00"})", "text").empty());
    assert(tv::json_string_member(R"({"text":"unterminated})", "text").empty());
    assert(tv::json_string_member(R"({"old":"a \"text\": \"fake\"","text":"real"})", "text") == "real");
    assert(tv::json_string_member(R"({"text":"line\b\f\t\/end"})", "text") == "line\b\f\t/end");
    std::cout << "JSON text: fallback field, escaping, Unicode pairs and malformed archives passed\n";
}
