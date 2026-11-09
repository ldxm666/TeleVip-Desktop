#include "archive_loader.h"
#include <atomic>
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <thread>

static tv::ArchiveLoader::Result finish(tv::ArchiveLoader& loader) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        if (auto result = loader.poll()) return std::move(*result);
        std::this_thread::yield();
    }
    throw std::runtime_error("archive loader did not finish");
}

int main() {
    std::promise<void> entered, release;
    auto released = release.get_future().share();
    std::atomic<int> calls{0}, active{0}, maximum{0};
    tv::ArchiveLoader loader([&](size_t offset) {
        const int count = ++active;
        maximum.store(std::max(maximum.load(), count));
        ++calls;
        if (offset == 0) { entered.set_value(); released.wait(); }
        tv::ArchiveRow row; row.msg = static_cast<int>(offset + 1);
        --active;
        return std::vector<tv::ArchiveRow>{row};
    });
    loader.request(0);
    entered.get_future().wait();
    // These operations finish while a fake disk is blocked. No timing
    // threshold or sleep is needed to prove the UI does not wait for IO.
    for (size_t offset = 5; offset <= 500; offset += 5) loader.request(offset);
    const bool did_not_wait = !loader.poll().has_value() && calls == 1;
    release.set_value();
    const auto result = finish(loader);
    assert(did_not_wait);
    assert(result.offset == 500 && result.rows.front().msg == 501);
    assert(calls == 2 && maximum == 1);
    assert(!loader.poll());

    tv::ArchiveLoader failing([](size_t) -> std::vector<tv::ArchiveRow> {
        throw std::runtime_error("fixture IO error");
    });
    failing.request(10);
    const auto error = finish(failing);
    assert(error.offset == 10 && error.failed);
    std::cout << "archive loader: nonblocking, coalescing, stale-result and error tests passed\n";
}
