// Latch semantics for the native translate bar.
//
// The bar is one toggle button: click it twice and the chat flips back to the
// original language ("回弹"). These tests pin the exact decision ladder, and
// they pin the fact that every decision is logged - a guard nobody can observe
// cannot be proven to work.
#include "translate_click_guard.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const std::string& message) {
    if (!ok) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void expect_reason(const tv::TranslateClickGuard& guard, tv::GuardReason reason,
                   const std::string& message) {
    if (guard.last_verdict().reason != reason) {
        ++failures;
        std::cerr << "FAIL: " << message << " (got reason #"
                  << static_cast<int>(guard.last_verdict().reason) << ", want #"
                  << static_cast<int>(reason) << ")\n";
    }
}

constexpr uint32_t kInput = 7;
constexpr uint64_t kSlot = 0x1234;

bool allow(tv::TranslateClickGuard& guard, const wchar_t* key, uint64_t now,
           bool explicit_request = false, uint32_t input = kInput) {
    return guard.observe(key, true, false, now, explicit_request, input, kSlot, key);
}

void first_click_cycle() {
    tv::TranslateClickGuard guard;
    check(!allow(guard, L"chat", 1000), "first frame alone never clicks");
    expect_reason(guard, tv::GuardReason::NeedsStableFrames, "first frame is not stable yet");
    check(allow(guard, L"chat", 1400), "second frame 400 ms later may click");
    expect_reason(guard, tv::GuardReason::AllowAutomatic, "fresh chat allows one click");
    guard.delivered();
    // The repaint window: the label still reads "translate" for a while.
    for (const uint64_t now : {1500ull, 2200ull, 5000ull}) {
        check(!allow(guard, L"chat", now), "stale pending frame after a click is refused");
        expect_reason(guard, tv::GuardReason::Cooldown, "refused by the per-key cooldown");
    }
    check(guard.last_verdict().wait_ms > 0, "a cooldown denial reports the remaining wait");
}

void original_is_terminal() {
    tv::TranslateClickGuard guard;
    allow(guard, L"chat", 1000);
    allow(guard, L"chat", 1400);
    guard.delivered();
    check(!guard.observe(L"chat", true, true, 2000), "bar reading original never clicks");
    expect_reason(guard, tv::GuardReason::BarOriginal, "original label is a hard deny");
    // OCR flap: pending right after original must not re-arm the toggle.
    check(!allow(guard, L"chat", 2100), "pending right after original is an OCR flap");
    expect_reason(guard, tv::GuardReason::AlreadyTranslated, "translated latch blocks the flap");
    check(!allow(guard, L"chat", 60000), "flap protection remains active");
    expect_reason(guard, tv::GuardReason::AlreadyTranslated, "latch survives beyond 90 seconds");
    check(!allow(guard, L"chat", 100000), "stale pending OCR never rearms automatic mode");
    expect_reason(guard, tv::GuardReason::AlreadyTranslated, "long-lived pending frame stays terminal");
    check(guard.last_verdict().clicks == 1, "automatic mode keeps the single delivered click");
    check(guard.observe(L"chat", true, false, 100400, true, kInput + 1, kSlot, L"chat"),
          "an explicit user request can still toggle after the flap guard");
    expect_reason(guard, tv::GuardReason::AllowExplicit, "explicit retry is named");
}

void preexisting_translation_is_terminal() {
    tv::TranslateClickGuard guard;
    check(!guard.observe(L"chat", true, true, 1000),
          "an already translated chat is never auto-clicked");
    expect_reason(guard, tv::GuardReason::BarOriginal, "original label is detected");
    check(!allow(guard, L"chat", 100000),
          "a stale pending frame cannot undo a preexisting translation");
    expect_reason(guard, tv::GuardReason::AlreadyTranslated,
                  "preexisting translated state remains terminal");
}

// A confirmed translation is terminal. The settle step observes the bar flip to
// "Show original"; from that moment on no scan may click this key again, however
// stale the frame it reads. This is the state the 2026-11-03 rebound never had.
void confirmed_is_terminal() {
    tv::TranslateClickGuard guard;
    allow(guard, L"chat", 1000);
    allow(guard, L"chat", 1400);
    guard.delivered();
    guard.confirm_translated(L"chat", 1600);
    check(!allow(guard, L"chat", 1000000), "confirmed chat is never clicked again");
    expect_reason(guard, tv::GuardReason::AlreadyTranslated, "terminal state is named in the log");
    check(guard.last_verdict().clicks == 1, "terminal state keeps the click count");
    check(!allow(guard, L"chat", 1200000), "the terminal state does not decay with time");
    expect_reason(guard, tv::GuardReason::AlreadyTranslated, "still terminal a minute later");
    // An explicit request (hotkey / tool page / overlay button) is the only way
    // back in, and it still pays the per-key cooldown.
    check(guard.observe(L"chat", true, false, 1300000, true, kInput + 1, kSlot, L"chat"),
          "explicit request still works on a terminal chat");
    expect_reason(guard, tv::GuardReason::AllowExplicit, "explicit allow is named");
}
// The live loop scans every ~800 ms (kBarScanMs). Reproduce exactly that: after the
// settle confirmed the flip to "Show original", three consecutive scans must all be
// denied as AlreadyTranslated with the click count frozen at 1. That is the
// no-rebound proof in miniature, and it is the shape native.log has to show live.
void confirmed_survives_three_scan_cycles() {
    tv::TranslateClickGuard guard;
    std::vector<std::wstring> lines;
    guard.set_logger([&lines](const std::wstring& line) { lines.push_back(line); });
    allow(guard, L"chat", 1000);
    allow(guard, L"chat", 1400);
    guard.delivered();
    guard.observe(L"chat", true, true, 2000, false, kInput, kSlot, L"chat");
    guard.confirm_translated(L"chat", 2100);
    const size_t before = lines.size();
    for (int cycle = 1; cycle <= 3; ++cycle) {
        const uint64_t now = 2900 + static_cast<uint64_t>(cycle) * 800;
        check(!allow(guard, L"chat", now), "scan cycle " + std::to_string(cycle) + " must not click");
        expect_reason(guard, tv::GuardReason::AlreadyTranslated,
                      "scan cycle " + std::to_string(cycle) + " is terminal");
        check(guard.last_verdict().clicks == 1,
              "scan cycle " + std::to_string(cycle) + " keeps the single click");
    }
    check(lines.size() == before + 3, "three scans write exactly three decision lines");
    for (size_t i = before; i < lines.size(); ++i) {
        check(lines[i].find(L"already_translated") != std::wstring::npos,
              "every scan line names the terminal reason");
        check(lines[i].find(L"chat=\"chat\"") != std::wstring::npos,
              "every scan line still carries the chat identity");
    }
}



void settle_failure_needs_real_input() {
    tv::TranslateClickGuard guard;
    allow(guard, L"chat", 1000);
    allow(guard, L"chat", 1400);
    guard.delivered();
    guard.settle_failed(L"chat", 1500);
    check(!allow(guard, L"chat", 3000), "failed settle blocks automatic retries");
    expect_reason(guard, tv::GuardReason::SettlePending, "settle failure needs user input");
    check(!allow(guard, L"chat", 3000, false, kInput + 1), "new input still waits out the cooldown");
    expect_reason(guard, tv::GuardReason::Cooldown, "settle cooldown is separate");
    check(!allow(guard, L"chat", 20000, false, kInput + 1), "still needs a stable frame");
    check(allow(guard, L"chat", 20400, false, kInput + 1), "after input and cooldown it may retry");
}

void missing_bar_is_a_noop() {
    tv::TranslateClickGuard guard;
    check(!allow(guard, L"chat", 1000), "no stable frame yet");
    check(!guard.observe(L"chat", false, false, 1100), "absent bar never clicks");
    expect_reason(guard, tv::GuardReason::NoBar, "absent bar is a no-op");
    check(!allow(guard, L"chat", 1200), "the stability chain restarts after a miss");
    expect_reason(guard, tv::GuardReason::NeedsStableFrames, "chain restarted");
    check(!guard.observe(L"", true, false, 1300), "bar without a chat identity never clicks");
    expect_reason(guard, tv::GuardReason::NoKey, "missing identity is a hard deny");
}

void renamed_heading_same_slot() {
    tv::TranslateClickGuard guard;
    allow(guard, L"a", 1000);
    allow(guard, L"a", 1400);
    guard.delivered();
    // OCR renamed the chat but the bar sits in the same slot and the user has
    // not touched anything: that is not navigation.
    check(!guard.observe(L"a-typo", true, false, 2000, false, kInput, kSlot),
          "same slot without user input is the same toggle");
    expect_reason(guard, tv::GuardReason::InputChanged, "same slot is refused by name");
    // Real navigation (new input tick) unlocks a new chat after stable frames.
    check(!guard.observe(L"b", true, false, 3000, false, kInput + 1, kSlot + 9),
          "new chat still needs a stable frame");
    check(guard.observe(L"b", true, false, 3400, false, kInput + 1, kSlot + 9),
          "new chat after real input may click");
}

void explicit_request_path() {
    tv::TranslateClickGuard guard;
    check(guard.observe(L"chat", true, false, 1000, true, kInput, kSlot, L"chat"),
          "explicit request skips the observation gate");
    expect_reason(guard, tv::GuardReason::AllowExplicit, "explicit allow is named");
    guard.delivered();
    check(!guard.observe(L"chat", true, false, 2000, true, kInput, kSlot, L"chat"),
          "second explicit press is refused by the cooldown");
    guard.confirm_translated(L"chat", 2500);
    check(!guard.observe(L"chat", true, false, 3000, true, kInput + 1, kSlot, L"chat"),
          "explicit press right after original is refused");
    expect_reason(guard, tv::GuardReason::RecentOriginal, "flap guard also covers explicit");
    check(guard.observe(L"chat", true, false, 9000, true, kInput + 2, kSlot, L"chat"),
          "explicit press after the short flap guard is allowed");
}

void click_limit() {
    tv::TranslateClickGuard guard;
    uint64_t now = 1000;
    for (unsigned i = 0; i < tv::TranslateClickGuard::kMaxClicksPerKey; ++i) {
        allow(guard, L"loop", now);
        if (!allow(guard, L"loop", now + 400)) {
            check(false, "click " + std::to_string(i) + " should be allowed");
            return;
        }
        guard.delivered();
        now += 400 + tv::TranslateClickGuard::kMaxCooldownMs + 1000;
    }
    check(!allow(guard, L"loop", now + 400), "click cap closes the key forever");
    expect_reason(guard, tv::GuardReason::ClickLimit, "click limit is named");
}

void logging_is_one_line_per_decision() {
    std::vector<std::wstring> lines;
    tv::TranslateClickGuard guard;
    guard.set_logger([&lines](const std::wstring& line) { lines.push_back(line); });
    allow(guard, L"chat", 1000);
    allow(guard, L"chat", 1400);
    guard.delivered();
    guard.observe(L"chat", true, true, 2000);
    if (lines.size() != 4) {
        check(false, "every decision must log exactly one line");
        return;
    }
    check(lines[0].find(L"-> DENY needs_stable_frames") != std::wstring::npos,
          "deny line carries the reason");
    check(lines[1].find(L"-> ALLOW allow_automatic") != std::wstring::npos,
          "allow line carries the reason");
    check(lines[2].find(L"-> CLICK #1") != std::wstring::npos, "click line carries the count");
    check(lines[3].find(L"-> DENY bar_original") != std::wstring::npos,
          "original line carries the reason");
    for (const auto& line : lines) {
        check(line.find(L"key=") != std::wstring::npos, "every line carries the key");
        check(line.find(L"chat=\"chat\"") != std::wstring::npos, "every line carries the chat name");
    }
}

void reset_forgets_everything() {
    tv::TranslateClickGuard guard;
    allow(guard, L"chat", 1000);
    allow(guard, L"chat", 1400);
    guard.delivered();
    guard.observe(L"chat", true, true, 1500);
    guard.reset();
    check(!allow(guard, L"chat", 1600), "after reset the stability gate starts over");
    check(allow(guard, L"chat", 2000), "after reset a fresh chat may click again");
}

} // namespace

int main() {
    first_click_cycle();
    original_is_terminal();
    preexisting_translation_is_terminal();
    confirmed_is_terminal();
    confirmed_survives_three_scan_cycles();
    settle_failure_needs_real_input();
    missing_bar_is_a_noop();
    renamed_heading_same_slot();
    explicit_request_path();
    click_limit();
    logging_is_one_line_per_decision();
    reset_forgets_everything();
    if (failures) return 1;
    std::cout << "translate click guard: persistent translated latch, cooldown, settle, "
                 "click cap, key stability and decision logging passed\n";
    return 0;
}
