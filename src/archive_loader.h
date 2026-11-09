#pragma once

#include "tools.h"
#include <chrono>
#include <functional>
#include <future>
#include <optional>
#include <utility>

namespace tv {

// Only one disk read may be in flight. The UI submits an offset and polls a
// ready result without waiting; rapid paging coalesces to the latest offset.
// The task owns its loader and query, and never captures a window or App.
class ArchiveLoader {
public:
    struct Result {
        size_t offset = 0;
        std::vector<ArchiveRow> rows;
        bool failed = false;
    };
    using Loader = std::function<std::vector<ArchiveRow>(size_t)>;
    explicit ArchiveLoader(Loader loader) : loader_(std::move(loader)) {}

    void request(size_t offset) {
        desired_ = offset;
        if (!task_.valid()) launch(offset);
    }

    std::optional<Result> poll() {
        if (launch_error_) return std::exchange(launch_error_, std::nullopt);
        if (!task_.valid() || task_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return std::nullopt;
        Result result = task_.get();
        if (result.offset != desired_) {
            launch(desired_);
            return std::nullopt;
        }
        return result;
    }

private:
    void launch(size_t offset) {
        const auto loader = loader_;
        try {
            task_ = std::async(std::launch::async, [loader, offset] {
                try { return Result{offset, loader(offset), false}; }
                catch (...) { return Result{offset, {}, true}; }
            });
        } catch (...) { launch_error_ = Result{offset, {}, true}; }
    }
    Loader loader_;
    std::future<Result> task_;
    std::optional<Result> launch_error_;
    size_t desired_ = 0;
};

} // namespace tv
