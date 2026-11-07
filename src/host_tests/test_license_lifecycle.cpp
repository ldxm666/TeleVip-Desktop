#include "../license_lifecycle.h"
#include <cassert>
#include <iostream>

int main() {
    using namespace tv::license;
    assert(classify_http(true, 401, "session_expired") == FailureKind::SessionLost);
    assert(classify_http(true, 403, "") == FailureKind::SessionLost); // Compatible with an older server.
    assert(classify_http(true, 403, "license_invalid") == FailureKind::Rejected);
    assert(classify_http(false, 403, "") == FailureKind::Rejected);
    assert(classify_http(true, 429, "") == FailureKind::Transient);
    assert(classify_http(true, 502, "") == FailureKind::Transient);
    assert(classify_http(true, 409, "") == FailureKind::Transient);
    RenewalPolicy policy;
    for (unsigned delay : {5u, 10u, 20u, 30u, 60u, 60u, 60u}) {
        const auto decision = policy.failed(FailureKind::Transient);
        assert(!decision.terminal && decision.delay_seconds == delay);
    }
    policy.succeeded();
    assert(policy.failed(FailureKind::Transient).delay_seconds == 5);
    assert(policy.failed(FailureKind::Transient, 120).delay_seconds == 120);
    assert(policy.failed(FailureKind::Rejected).terminal);
    assert(policy.failed(FailureKind::RestartRequired).terminal);
    assert(!policy.failed(FailureKind::InvalidResponse).terminal);
    SuspendedFeatures features;
    constexpr std::uint32_t wanted = 0x400051;
    assert(features.suspend(wanted));
    assert(features.paused() && features.preferred(0) == wanted);
    assert(!features.suspend(0));
    assert(features.preferred(0) == wanted);
    assert(features.resume() == wanted);
    assert(!features.paused() && !features.resume());
    assert(features.suspend(0));
    const auto off = features.resume();
    assert(off.has_value() && *off == 0);
    for (unsigned cycle = 0; cycle < 100; ++cycle) {
        const std::uint32_t expected = 1u << (cycle % 30);
        assert(features.suspend(expected));
        assert(features.preferred(0) == expected);
        assert(features.resume() == expected);
    }
    std::cout << "license lifecycle: recoverable sessions, bounded backoff, Retry-After, real rejection and 100 preference pause/resume cycles passed\n";
}
