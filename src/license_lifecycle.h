#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace tv::license {

enum class FailureKind { None, Transient, SessionLost, Rejected, InvalidResponse, RestartRequired };

struct RequestFailure {
    FailureKind kind = FailureKind::Transient;
    std::uint32_t http = 0;
    std::uint32_t winhttp = 0;
    unsigned retry_after = 0;
};

inline FailureKind classify_http(bool refresh, std::uint32_t status, std::string_view reason) {
    if (status == 200) return FailureKind::None;
    if (status == 401 || (refresh && status == 403 && reason != "license_invalid"))
        return FailureKind::SessionLost;
    if (status == 403 || (!refresh && status == 400)) return FailureKind::Rejected;
    return FailureKind::Transient;
}

struct RenewalDecision {
    bool terminal = false;
    unsigned delay_seconds = 60;
};

class RenewalPolicy {
public:
    void succeeded() noexcept { failures_ = 0; }
    RenewalDecision failed(FailureKind kind, unsigned retry_after = 0) noexcept {
        if (kind == FailureKind::Rejected || kind == FailureKind::RestartRequired) return {true, 0};
        constexpr std::array<unsigned, 5> backoff{5, 10, 20, 30, 60};
        const unsigned delay = backoff[std::min(failures_, backoff.size() - 1)];
        if (failures_ < backoff.size() - 1) ++failures_;
        return {false, std::max(delay, retry_after)};
    }
private:
    std::size_t failures_ = 0;
};

// Desired feature settings survive a lease pause, including an intentional all-off state.
class SuspendedFeatures {
public:
    bool suspend(std::uint32_t current) noexcept {
        if (paused_) return false;
        saved_ = current;
        paused_ = true;
        return true;
    }
    std::optional<std::uint32_t> resume() noexcept {
        if (!paused_) return std::nullopt;
        paused_ = false;
        return saved_;
    }
    bool paused() const noexcept { return paused_; }
    std::uint32_t preferred(std::uint32_t current) const noexcept { return paused_ ? saved_ : current; }
private:
    bool paused_ = false;
    std::uint32_t saved_ = 0;
};
} // namespace tv::license
