#include "motion.h"
#include <cassert>
#include <iostream>

int main() {
    using namespace tv::motion;
    for (int fps : {30, 60, 120, 144}) {
        for (auto spec : {kNavigation, kHover, kToggle, kPress, kToast}) {
            Spring spring;
            spring.target = 700;
            for (int i = 0; i < fps; ++i) {
                spring.update(1.0f / fps, spec);
                assert(std::isfinite(spring.value) && std::isfinite(spring.velocity));
                assert(spring.value >= -14 && spring.value <= 714);
            }
            assert(spring.value == 700 && spring.velocity == 0);
            spring.target = 300;
            spring.update(1.0f / fps, spec);
            const float v = spring.value;
            spring.target = 500;
            assert(spring.value == v); // Retargeting never jumps the current presentation.
            for (int i = 0; i < fps; ++i) spring.update(1.0f / fps, spec);
            assert(spring.value == 500);
        }
        Tween page;
        page.target = 1;
        float previous = 0;
        for (int i = 0; i < fps; ++i) {
            page.update(1.0f / fps);
            assert(page.value >= previous && page.value <= 1);
            previous = page.value;
        }
        assert(page.value == 1);
        page.snap(page.target);
        assert(!page.update(0.016f));
    }
    std::cout << "motion: 30/60/120/144Hz, bounded springs, retargeting, tween and resting state passed\n";
}
