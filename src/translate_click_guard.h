#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace tv {

// Why a translate-bar click was allowed or denied.
//
// The bar is a single toggle button (history_view_translate_bar.cpp:298-300:
// `translateTo(history->translatedTo() ? LanguageId() : _to.current())`), so a
// second click on the same bar flips the chat back to the original language.
// That is the "回弹" the user sees. Every decision therefore has to be named and
// recorded: a guard nobody can observe cannot be proven to work, and the
// 2026-11-03 rebound survived exactly because allow/deny left no trace.
enum class GuardReason {
    NoBar,             // no translate bar on screen -> no-op, never a retry storm
    NoKey,             // bar found but the chat could not be identified
    BarOriginal,       // bar already reads "Show original" -> terminal for this key
    AlreadyTranslated, // original label was observed; auto mode never re-clicks
    RecentOriginal,    // saw Original moments ago -> a Pending frame is an OCR flap
    Cooldown,          // too soon after the previous click for this key
    SettlePending,     // last click never flipped the label; needs real user input
    ClickLimit,        // hard cap of clicks reached for this key
    NeedsStableFrames, // not observed long enough / frames not stable yet
    InputChanged,      // same physical bar as the last click, no user action between
    AllowAutomatic,    // first click for a freshly seen chat
    AllowExplicit,     // user asked for it (hotkey / tool page / overlay button)
};

inline const wchar_t* guard_reason_name(GuardReason reason) {
    switch (reason) {
    case GuardReason::NoBar: return L"no_bar";
    case GuardReason::NoKey: return L"no_key";
    case GuardReason::BarOriginal: return L"bar_original";
    case GuardReason::AlreadyTranslated: return L"already_translated";
    case GuardReason::RecentOriginal: return L"recent_original";
    case GuardReason::Cooldown: return L"cooldown";
    case GuardReason::SettlePending: return L"settle_pending";
    case GuardReason::ClickLimit: return L"click_limit";
    case GuardReason::NeedsStableFrames: return L"needs_stable_frames";
    case GuardReason::InputChanged: return L"input_changed";
    case GuardReason::AllowAutomatic: return L"allow_automatic";
    case GuardReason::AllowExplicit: return L"allow_explicit";
    }
    return L"unknown";
}

struct GuardVerdict {
    bool allow = false;
    GuardReason reason = GuardReason::NoBar;
    unsigned observations = 0;
    uint64_t age_ms = 0;   // how long this candidate has been under observation
    unsigned clicks = 0;   // clicks already delivered for this key
    uint64_t wait_ms = 0;  // remaining cooldown / settle wait when denied
};

inline uint64_t guard_hash(std::wstring_view text) {
    uint64_t hash = 1469598103934665603ull; // FNV-1a, stable across runs
    for (const wchar_t ch : text) {
        hash ^= static_cast<uint64_t>(ch);
        hash *= 1099511628211ull;
    }
    return hash;
}

inline std::wstring guard_hex(uint64_t value) {
    static const wchar_t digits[] = L"0123456789abcdef";
    wchar_t buffer[17] = {};
    for (int i = 15; i >= 0; --i) {
        buffer[i] = digits[value & 0xF];
        value >>= 4;
    }
    return std::wstring(buffer, 16);
}

// Per-chat latch for the native translate bar.
//
// Key is the chat identity (window title / OCR heading), never the bar label:
// the label flips 翻译 -> 显示原文 right after a click, so a key derived from it
// would look like a brand new chat and re-arm the toggle. The original text is
// the only stable identity.
//
// Life cycle per key:
//   Unknown --(Pending, 2 stable frames)--> allow -> Clicked
//   Clicked --(Original seen)--> Translated  (terminal for auto mode)
//   Clicked --(label did not flip)--> SettleFailed (needs real user input)
//   Translated --(Pending again)--> deny automatic clicks until explicit request
class TranslateClickGuard {
public:
    using Logger = std::function<void(const std::wstring&)>;

    // Tuned against the observed failure: two clicks 1.95 s apart on the same
    // chat toggled the translation off. 6 s is already far outside that window;
    // the doubling keeps a misbehaving recogniser from ever clicking in bursts.
    static constexpr uint64_t kStableGapMs = 350;
    static constexpr uint64_t kObservationWindowMs = 1500;
    static constexpr uint64_t kExplicitOriginalGuardMs = 3000;
    static constexpr uint64_t kFirstCooldownMs = 6000;
    static constexpr uint64_t kMaxCooldownMs = 60000;
    static constexpr uint64_t kSettleCooldownMs = 12000;
    static constexpr unsigned kMaxClicksPerKey = 8;
    static constexpr unsigned kStableObservations = 2;
    static constexpr size_t kMaxTrackedChats = 2048;

    void set_logger(Logger logger) { logger_ = std::move(logger); }
    bool has_logger() const { return static_cast<bool>(logger_); }
    const GuardVerdict& last_verdict() const { return last_; }

    // Returns true when the caller may click the bar exactly once. The caller
    // must then call delivered() before/while dispatching the click.
    bool observe(const std::wstring& key, bool found, bool original, uint64_t now,
                 bool explicit_request = false, uint32_t input_tick = 0,
                 uint64_t slot_id = 0, const std::wstring& label = std::wstring()) {
        now_ = now;
        pending_input_ = input_tick;
        pending_slot_ = slot_id;
        last_label_ = label;
        last_found_ = found;
        last_original_ = original;
        last_explicit_ = explicit_request;

        GuardVerdict verdict;
        if (!found) {
            reset_candidate();
            return finish(key, verdict, GuardReason::NoBar);
        }
        if (key.empty()) {
            reset_candidate();
            return finish(key, verdict, GuardReason::NoKey);
        }

        State& state = state_for(key);
        verdict.clicks = state.clicks;
        const uint64_t since_original =
            state.last_original && now >= state.last_original ? now - state.last_original : 0;

        if (original) {
            // Seeing the original-view action is proof the chat is translated.
            // Keep this terminal latch for automatic scans; OCR may later report
            // a stale Pending frame after Telegram repaints the bar.
            state.phase = Phase::Translated;
            state.last_original = now;
            state.observations = 0;
            reset_candidate();
            return finish(key, verdict, GuardReason::BarOriginal);
        }

        // The bar is a toggle. Once "Show original" was observed, automatic
        // scans must never click it again, even if a stale OCR frame says Pending.
        // An explicit user request still goes through the cooldown ladder below.
        if (state.phase == Phase::Translated && !explicit_request) {
            reset_candidate();
            return finish(key, verdict, GuardReason::AlreadyTranslated);
        }

        if (state.clicks >= kMaxClicksPerKey) {
            reset_candidate();
            return finish(key, verdict, GuardReason::ClickLimit);
        }
        // A Pending frame right after Original is the toggle mid-repaint, not a
        // new invitation to click. This is the flap that produced the rebound.
        if (state.phase == Phase::Translated && explicit_request &&
            since_original < kExplicitOriginalGuardMs) {
            reset_candidate();
            verdict.wait_ms = kExplicitOriginalGuardMs - since_original;
            return finish(key, verdict, GuardReason::RecentOriginal);
        }
        if (state.phase == Phase::SettleFailed && !explicit_request) {
            if (input_tick == state.click_input) {
                reset_candidate();
                return finish(key, verdict, GuardReason::SettlePending);
            }
            const uint64_t since_click = now >= state.last_click ? now - state.last_click : 0;
            if (since_click < kSettleCooldownMs) {
                reset_candidate();
                verdict.wait_ms = kSettleCooldownMs - since_click;
                return finish(key, verdict, GuardReason::Cooldown);
            }
        }
        if (state.clicks > 0) {
            const uint64_t cooldown = cooldown_for(state);
            const uint64_t since_click = now >= state.last_click ? now - state.last_click : 0;
            if (since_click < cooldown) {
                reset_candidate();
                verdict.wait_ms = cooldown - since_click;
                return finish(key, verdict, GuardReason::Cooldown);
            }
        }
        // Same physical bar, same user input as the click we already delivered:
        // a renamed OCR heading is not navigation.
        if (!explicit_request && delivered_any_ && state.clicks == 0 &&
            input_tick == last_click_input_ && slot_id != 0 && slot_id == last_click_slot_) {
            reset_candidate();
            return finish(key, verdict, GuardReason::InputChanged);
        }

        // Two independent frames of the same bar, at least kStableGapMs apart.
        const bool continuity = candidate_ == key && now >= last_seen_ &&
                                now - last_seen_ <= kObservationWindowMs &&
                                candidate_input_ == input_tick;
        if (!continuity) {
            candidate_ = key;
            since_ = now;
            observations_ = 0;
            candidate_input_ = input_tick;
        }
        ++observations_;
        last_seen_ = now;
        verdict.observations = observations_;
        verdict.age_ms = now - since_;

        if (!explicit_request &&
            (observations_ < kStableObservations || now - since_ < kStableGapMs)) {
            return finish(key, verdict, GuardReason::NeedsStableFrames);
        }
        const GuardReason allow = explicit_request ? GuardReason::AllowExplicit
                                                   : GuardReason::AllowAutomatic;
        return finish(key, verdict, allow);
    }

    // Call once, immediately before dispatching the click. Records the latch so
    // a stale frame can never click the same toggle twice.
    void delivered() {
        if (last_key_.empty()) return;
        State& state = state_for(last_key_);
        state.phase = Phase::Clicked;
        state.clicks += 1;
        state.last_click = now_;
        state.click_input = last_input_;
        delivered_any_ = true;
        last_click_input_ = last_input_;
        last_click_slot_ = last_slot_;
        emit_raw(L"guard key=" + guard_hex(guard_hash(last_key_)) + L" chat=\"" +
                 display_label(last_key_) + L"\" -> CLICK #" + std::to_wstring(state.clicks) +
                 L" (cooldown " + std::to_wstring(cooldown_for(state)) + L"ms)");
        reset_candidate();
    }

    // The bar flipped to "Show original" after our click: the latch is terminal.
    void confirm_translated(const std::wstring& key, uint64_t now) {
        if (key.empty()) return;
        State& state = state_for(key);
        state.phase = Phase::Translated;
        state.last_original = now;
        state.observations = 0;
        emit_raw(L"guard key=" + guard_hex(guard_hash(key)) +
                 L" -> CONFIRMED translated (terminal for auto mode, clicks=" +
                 std::to_wstring(state.clicks) + L")");
    }

    // The label never flipped inside the settle window. Do not click again until
    // the user really interacts; the frame we acted on may simply have been stale.
    void settle_failed(const std::wstring& key, uint64_t now) {
        if (key.empty()) return;
        State& state = state_for(key);
        state.phase = Phase::SettleFailed;
        state.last_click = now;
        state.observations = 0;
        emit_raw(L"guard key=" + guard_hex(guard_hash(key)) +
                 L" -> SETTLE FAILED (label did not flip, latch closed)");
    }

    void reset() {
        states_.clear();
        delivered_any_ = false;
        last_key_.clear();
        last_click_input_ = 0;
        last_click_slot_ = 0;
        reset_candidate();
    }

private:
    enum class Phase { Unknown, Clicked, Translated, SettleFailed };

    struct State {
        Phase phase = Phase::Unknown;
        uint64_t last_original = 0;
        uint64_t last_click = 0;
        uint32_t click_input = 0;
        unsigned observations = 0;
        unsigned clicks = 0;
    };

    State& state_for(const std::wstring& key) {
        if (states_.size() >= kMaxTrackedChats) states_.clear();
        return states_[key];
    }

    static uint64_t cooldown_for(const State& state) {
        if (state.clicks == 0) return 0;
        const unsigned shift = state.clicks > 4 ? 4 : state.clicks - 1;
        const uint64_t scaled = kFirstCooldownMs << shift;
        return scaled > kMaxCooldownMs ? kMaxCooldownMs : scaled;
    }

    bool finish(const std::wstring& key, GuardVerdict& verdict, GuardReason reason) {
        verdict.reason = reason;
        verdict.allow = reason == GuardReason::AllowAutomatic ||
                        reason == GuardReason::AllowExplicit;
        last_ = verdict;
        if (verdict.allow) {
            last_key_ = key;
            last_input_ = pending_input_;
            last_slot_ = pending_slot_;
        }
        emit(key, verdict);
        return verdict.allow;
    }

    void emit(const std::wstring& key, const GuardVerdict& verdict) {
        if (!logger_) return;
        std::wstring line = L"guard key=" + guard_hex(guard_hash(key));
        line += L" chat=\"" + display_label(key) + L"\"";
        line += L" found=" + std::wstring(last_found_ ? L"1" : L"0");
        line += L" orig=" + std::wstring(last_original_ ? L"1" : L"0");
        line += L" explicit=" + std::wstring(last_explicit_ ? L"1" : L"0");
        line += L" obs=" + std::to_wstring(verdict.observations);
        line += L" age=" + std::to_wstring(verdict.age_ms);
        line += L" clicks=" + std::to_wstring(verdict.clicks);
        if (verdict.wait_ms) line += L" wait=" + std::to_wstring(verdict.wait_ms);
        line += verdict.allow ? L" -> ALLOW " : L" -> DENY ";
        line += guard_reason_name(verdict.reason);
        logger_(line);
    }

    void emit_raw(const std::wstring& line) {
        if (logger_) logger_(line);
    }

    // The chat name is what a human greps for; fall back to the key so no log
    // line is ever anonymous.
    std::wstring display_label(const std::wstring& key) const {
        return last_label_.empty() ? key : last_label_;
    }

    void reset_candidate() {
        candidate_.clear();
        observations_ = 0;
        candidate_input_ = 0;
    }

    std::unordered_map<std::wstring, State> states_;
    std::wstring candidate_;
    uint64_t since_ = 0;
    uint64_t last_seen_ = 0;
    unsigned observations_ = 0;
    uint32_t candidate_input_ = 0;

    std::wstring last_key_;
    uint32_t last_input_ = 0;
    uint64_t last_slot_ = 0;
    uint64_t pending_slot_ = 0;
    uint32_t pending_input_ = 0;
    bool delivered_any_ = false;
    uint32_t last_click_input_ = 0;
    uint64_t last_click_slot_ = 0;

    uint64_t now_ = 0;
    GuardVerdict last_;
    std::wstring last_label_;
    bool last_found_ = false;
    bool last_original_ = false;
    bool last_explicit_ = false;
    Logger logger_;
};

// One toggle, one latch: the automatic agent, the hotkey and the overlay button
// all address the same bar, so they must share the same state.
inline TranslateClickGuard& shared_translate_guard() {
    static TranslateClickGuard guard;
    return guard;
}

} // namespace tv
