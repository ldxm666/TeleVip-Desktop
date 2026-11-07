#pragma once
#include "common.h"

namespace tv::license {
bool startup(HINSTANCE instance, int argc, wchar_t** argv, bool& probe);
bool authorized();
std::wstring hook_path();
std::wstring status_text();
void request_refresh();
void shutdown();
} // namespace tv::license
