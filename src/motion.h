#pragma once

#include <algorithm>
#include <cmath>

namespace tv::motion {

inline constexpr float kFast = 0.18f;
inline constexpr float kScene = 0.20f;
inline constexpr float kPageTravel = 16.0f;
inline constexpr float kPressScale = 0.97f;
inline constexpr float kHoverLift = 1.2f;

struct SpringSpec { float stiffness; float damping; };
inline constexpr SpringSpec kNavigation{2000.0f, 82.0f};
inline constexpr SpringSpec kHover{1400.0f, 68.0f};
inline constexpr SpringSpec kToggle{1600.0f, 64.0f};
inline constexpr SpringSpec kPress{1800.0f, 72.0f};
inline constexpr SpringSpec kToast{1500.0f, 70.0f};

inline float out(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
}

// Solve the damped oscillator analytically: the feel stays stable at variable refresh rates.
struct Spring {
    float value = 0;
    float target = 0;
    float velocity = 0;

    void snap(float v) {
        value = target = last_target_ = v;
        velocity = elapsed_ = 0;
    }

    bool update(float dt, SpringSpec spec) {
        if (target != last_target_) {
            last_target_ = target;
            elapsed_ = 0;
        }
        if (value == target && velocity == 0) return false;
        dt = std::clamp(dt, 0.0f, 0.05f);
        if (dt == 0) return false;
        elapsed_ += dt;
        if (elapsed_ >= kScene) {
            snap(target);
            return true;
        }
        const float x = value - target;
        const float v = velocity;
        const float gamma = spec.damping * 0.5f;
        const float discriminant = spec.stiffness - gamma * gamma;
        float next = 0;
        if (discriminant > 0.001f) {
            const float omega = std::sqrt(discriminant);
            const float a = std::exp(-gamma * dt);
            const float cs = std::cos(omega * dt), sn = std::sin(omega * dt);
            const float b = (v + gamma * x) / omega;
            next = a * (x * cs + b * sn);
            velocity = a * (v * cs - (gamma * b + omega * x) * sn);
        } else if (discriminant < -0.001f) {
            const float root = std::sqrt(-discriminant);
            const float r1 = -gamma + root, r2 = -gamma - root;
            const float a = (v - r2 * x) / (r1 - r2), b = x - a;
            const float e1 = std::exp(r1 * dt), e2 = std::exp(r2 * dt);
            next = a * e1 + b * e2;
            velocity = r1 * a * e1 + r2 * b * e2;
        } else {
            const float a = std::exp(-gamma * dt), b = v + gamma * x;
            next = a * (x + b * dt);
            velocity = a * (v - gamma * b * dt);
        }
        value = target + next;
        if (std::fabs(next) < 0.0005f && std::fabs(velocity) < 0.02f) snap(target);
        return true;
    }

private:
    float last_target_ = 0;
    float elapsed_ = 0;
};

struct Tween {
    float value = 0;
    float target = 0;

    explicit Tween(float seconds = kScene) : duration_(seconds) {}
    void snap(float v) { value = target = from_ = last_target_ = v; elapsed_ = 0; }
    bool update(float dt) {
        if (value == target) return false;
        if (last_target_ != target) {
            from_ = value;
            last_target_ = target;
            elapsed_ = 0;
        }
        elapsed_ += std::clamp(dt, 0.0f, 0.05f);
        const float t = std::min(1.0f, elapsed_ / duration_);
        value = t == 1.0f ? target : from_ + (target - from_) * out(t);
        return true;
    }

private:
    float from_ = 0;
    float last_target_ = 0;
    float elapsed_ = 0;
    float duration_;
};

} // namespace tv::motion
