#pragma once
#include "common.h"
#include "news_model.h"

namespace tv::news {
struct Result { std::optional<Model> model; bool ok = false; bool manual = false; };
class Client {
public:
    Client() = default;
    ~Client();
    void start();
    void request();
    bool check_once(Model& result) { return fetch(result); } // Non-UI diagnostic path only.
    std::optional<Result> poll();
    void acknowledge(const std::string& revision);
    const std::string& acknowledged() const { return acknowledged_; }
private:
    void run();
    bool fetch(Model& result);
    std::mutex mutex_;
    std::condition_variable wake_;
    std::thread worker_;
    bool stop_ = false, requested_ = false;
    std::optional<Result> completed_;
    std::optional<Model> current_;
    std::string etag_, acknowledged_;
};
}
