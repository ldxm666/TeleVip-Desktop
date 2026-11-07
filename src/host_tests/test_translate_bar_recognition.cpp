// Recognition of the native translate bar and of the chat identity it belongs to.
//
// Geometry is taken from the live 144 DPI window (client 1706x1018, chat name at
// 1163,55..1273,75, bar row at y=103): the previous 38 DIP / 22 DIP clearances
// rejected exactly that layout, which left the bar without a chat identity and
// made the agent refuse to click at all.
#include "translate_bar_recognition.h"

#include <iostream>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const char* message) {
    if (!ok) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
void check(bool ok, const std::string& message) {
    if (!ok) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}

void layout_test(UINT dpi, LONG offset, LONG pane, LONG width, LONG caption = 32) {
    const auto px = [dpi](LONG value) { return value * static_cast<LONG>(dpi) / 96; };
    const auto rect = [&](LONG left, LONG top, LONG right, LONG bottom) {
        return RECT{offset + px(left), 100 + px(top), offset + px(right), 100 + px(bottom)};
    };
    const RECT client = rect(0, 0, width, 700);
    const LONG center = (pane + width) / 2 + 12;
    std::vector<tv::OcrLineHit> lines{
        {L"Telegram (42)", rect(8, 6, 140, 22)},
        {L"H e l p C h a t", rect(pane + 20, caption + 8, pane + 150, caption + 26)},
        {L"online / 1,234 members", rect(pane + 20, caption + 30, pane + 180, caption + 46)},
        {L"Translate to English", rect(center - 90, caption + 63, center + 90, caption + 81)},
    };
    if (!caption) lines.erase(lines.begin());
    auto hit = tv::recognize_translate_bar(lines, client, dpi);
    check(hit.found && !hit.already && hit.chat == L"helpchat", "pane/heading at varied sidebar, DPI and screen origin");
    // A left-list preview precedes the real bar in the OCR result.
    lines.insert(lines.begin(), {L"Translate to Chinese", rect(30, 70, 210, 88)});
    hit = tv::recognize_translate_bar(lines, client, dpi);
    check(hit.found && hit.chat == L"helpchat" && hit.rect.left == offset + px(center - 90),
          "left chat-list preview is not the translate bar");
    lines.push_back({L"View Original (English)", rect(center - 90, caption + 63, center + 90, caption + 81)});
    hit = tv::recognize_translate_bar(lines, client, dpi);
    check(hit.found && hit.already, "Original takes precedence over conflicting OCR engines");
}

// The layout measured on the live window: 144 DPI, client 1706x1018 at 0,0,
// chat name "MapAdkiller" at y 55..75, bar row at y 103..121.
void measured_layout() {
    const RECT client{0, 0, 1706, 1018};
    const std::vector<tv::OcrLineHit> lines{
        {L"MapAdkiller", RECT{1163, 55, 1273, 75}},
        {L"0 \u8fde\u63a5\u4e2d...", RECT{1165, 85, 1210, 99}},
        {L"\u7ffb\u8bd1\u4e3a \u4e2d\u6587", RECT{1358, 103, 1498, 121}},
        {L"16:26", RECT{1617, 140, 1660, 152}},
    };
    const auto hit = tv::recognize_translate_bar(lines, client, 144);
    check(hit.found && !hit.already, "measured 144 DPI layout is recognised");
    check(hit.chat == L"mapadkiller", "measured layout yields the chat identity");
    // The old thresholds needed a 38 DIP gap and 22 DIP clearance; this layout
    // has 48 px (32 DIP) and 28 px, so it produced chat.empty() and no click.
    check(hit.rect.top == 103, "measured bar row kept");

    const std::vector<tv::OcrLineHit> original{
        {L"MapAdkiller", RECT{1163, 55, 1273, 75}},
        {L"View Original (English)", RECT{1330, 103, 1526, 121}},
    };
    const auto already = tv::recognize_translate_bar(original, client, 144);
    check(already.found && already.already && already.chat == L"mapadkiller",
          "original label at the measured layout is terminal");
}

// A bar row holds exactly one text line. Anything else on that row means the
// capture crossed columns - the old full-width scan matched chat-list previews
// and the GUI's own title bar and clicked garbage.
void polluted_rows_are_rejected() {
    const RECT client{0, 0, 1000, 700};
    const std::vector<tv::OcrLineHit> polluted{
        {L"HelpChat", RECT{320, 60, 450, 78}},
        {L"Translate to English", RECT{422, 95, 602, 113}},
        {L"\u793c\u7269", RECT{700, 96, 740, 112}},
    };
    check(!tv::recognize_translate_bar(polluted, client, 96).found,
          "second text column on the bar row rejects the candidate");

    const std::vector<tv::OcrLineHit> two_engines{
        {L"HelpChat", RECT{320, 60, 450, 78}},
        {L"Translate to English", RECT{422, 95, 602, 113}},
        {L"Translate to English", RECT{422, 95, 602, 113}},
    };
    const auto hit = tv::recognize_translate_bar(two_engines, client, 96);
    check(hit.found && hit.chat == L"helpchat", "two engines reporting the label is not pollution");
}

void chat_identity() {
    using tv::normalize_chat_identity;
    check(normalize_chat_identity(L"\u200EMapAdkiller \u2013 (149660)") == L"mapadkiller",
          "tdesktop title: bidi mark + unread counter stripped");
    check(normalize_chat_identity(L"(1) \u200EGeneral Chat \u2013 (79794)") == L"general chat",
          "tdesktop title: leading unread prefix stripped");
    check(normalize_chat_identity(L"TeleVip \u2013 Telegram") == L"televip",
          "tdesktop title: application suffix stripped");
    check(normalize_chat_identity(L"Telegram") == L"",
          "no open chat is not a chat identity");
    check(normalize_chat_identity(L"\u200ETelegram (3)") == L"",
          "no open chat with unread counter is not a chat identity");
    check(normalize_chat_identity(L"Name @ Account \u2013 (5)") == L"name",
          "multi-account suffix stripped");
    check(normalize_chat_identity(L"\u4e2d\u6587\u7fa4 \u2013 (2)") == L"\u4e2d\u6587\u7fa4",
          "CJK chat names survive normalisation");
    check(normalize_chat_identity(L"  Spaced   Name  ") == L"spaced name",
          "whitespace collapsed");
}
} // namespace

int main() {
    using tv::BarLabel;
    check(tv::classify_translate_bar(L"\u7ffb \u8bd1 \u4e3a \u4e2d \u6587") == BarLabel::Pending, "Chinese OCR spaces");
    check(tv::classify_translate_bar(L"TRANSLATE TO English") == BarLabel::Pending, "English case");
    check(tv::classify_translate_bar(L"View Original (English)") == BarLabel::Original, "automatic-channel original label");
    check(tv::classify_translate_bar(L"\u6aa2\u8996\u539f\u6587\uff08\u82f1\u6587\uff09") == BarLabel::Original, "traditional Chinese original label");
    check(tv::classify_translate_bar(L"Please Translate to English") == BarLabel::Missing, "message substring is not a control");
    check(tv::classify_translate_bar(L"user: \u663e\u793a\u539f\u6587") == BarLabel::Missing, "chat preview substring is not a control");
    check(tv::normalize_chat_heading(L"H e l p\u200BChat") == L"helpchat", "stable OCR chat identity");

    chat_identity();
    layout_test(96, 0, 300, 1280);
    layout_test(144, -1900, 700, 1440);
    layout_test(192, 100, 200, 1100);
    layout_test(96, -560, 0, 560, 0);
    measured_layout();
    polluted_rows_are_rejected();

    const RECT client{0, 0, 1000, 700};
    const auto no_heading = tv::recognize_translate_bar(
        {{L"Translate to English", RECT{422, 95, 602, 113}}}, client, 96);
    check(no_heading.found && no_heading.chat.empty(), "missing heading never becomes active-chat fallback");
    const auto tall_message = tv::recognize_translate_bar(
        {{L"Translate to English", RECT{422, 95, 602, 160}}}, client, 96);
    check(!tall_message.found, "multiline message is not a native bar label");
    const auto body_original = tv::recognize_translate_bar({
        {L"HelpChat", RECT{320, 132, 450, 150}},
        {L"Translate to English", RECT{712, 187, 892, 205}},
        {L"Show Original", RECT{742, 240, 862, 258}},
    }, RECT{0, 100, 1280, 800}, 96);
    if (!body_original.found || body_original.already || body_original.rect.top != 187) {
        std::cerr << "FAIL: body Original text overrides the real Pending bar\n";
        return 1;
    }
    if (failures) return 1;
    std::cout << "translate bar recognition: labels, geometry, identity and conflicting OCR passed\n";
    return 0;
}