#include "../telegram_link.h"
#include <cassert>
#include <future>
#include <iostream>
int main() {
    using namespace tv::telegram_link;
    assert(!command(L"file:///C:/bad.exe")); assert(!command(L"https://t.me.evil.test/x"));
    assert(!command(L"tg://resolve?domain=x\nCMD:quit;"));
    const auto escaped = command(L"tg://resolve?domain=x;CMD:quit&text=%中文");
    assert(escaped && escaped->find("x;CMD:quit") == std::string::npos && escaped->find("%003b") != std::string::npos);
    assert(escaped->find("%0025") != std::string::npos);
    assert(std::count(escaped->begin(), escaped->end(), ';') == 2);
    std::promise<void> ready;
    auto waiting = ready.get_future();
    std::string received;
    const auto pid = GetCurrentProcessId();
    std::thread server([&] {
        const std::wstring name = L"\\\\.\\pipe\\Global\\0123456789abcdef0123456789abcdef-TeleVipLinkTest-" + std::to_wstring(pid);
        HANDLE pipe = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1, 4096, 4096, 0, nullptr);
        assert(pipe != INVALID_HANDLE_VALUE); ready.set_value();
        assert(ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED);
        char bytes[1024]{}; DWORD count = 0; assert(ReadFile(pipe, bytes, sizeof(bytes), &count, nullptr));
        received.assign(bytes, count);
        const auto reply = "RES:" + std::to_string(pid) + "_0;";
        assert(WriteFile(pipe, reply.data(), static_cast<DWORD>(reply.size()), &count, nullptr));
        FlushFileBuffers(pipe); DisconnectNamedPipe(pipe); CloseHandle(pipe);
    });
    waiting.get();
    std::wstring error;
    assert(forward(pid, L"https://t.me/MapAdkillerr", error));
    server.join();
    assert(error.empty() && received == *command(L"https://t.me/MapAdkillerr"));
    assert(!forward(pid, L"https://t.me/MapAdkillerr", error));
    std::cout << "Telegram link: escaped protocol, kernel-verified PID, native pipe acknowledgement and closed-target failure passed\n";
}
