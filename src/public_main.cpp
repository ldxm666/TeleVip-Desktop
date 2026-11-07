#include "app.h"
#include <objbase.h>
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HRESULT initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    SetEnvironmentVariableW(L"TV_UI_PREVIEW",L"1");
    tv::App app(instance);
    if (!app.create()) { if(SUCCEEDED(initialized)) CoUninitialize(); return 1; }
    MSG message{};
    int state=0;
    while ((state=GetMessageW(&message,nullptr,0,0))>0) {
        const bool shortcut=(GetKeyState(VK_CONTROL)&0x8000)!=0 ||
            (message.message==WM_KEYDOWN && message.wParam==VK_ESCAPE);
        if(!shortcut && IsDialogMessageW(app.hwnd(),&message))continue;
        TranslateMessage(&message);DispatchMessageW(&message);
    }
    if(SUCCEEDED(initialized)) CoUninitialize();
    return state<0?1:static_cast<int>(message.wParam);
}
