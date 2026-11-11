# 构建公开源码 / Build the public source

## Windows x64

安装 Visual Studio 2022 或 Build Tools 的 **使用 C++ 的桌面开发**工作负载，以及 Windows 11 SDK（本版本使用 10.0.26100.0 验证）。需要 MSVC C++20、资源编译器及 SDK 的 C++/WinRT 头文件；无 Electron、Node.js 或 Rust 构建依赖。

在仓库根目录运行：

```bat
build.cmd
test.cmd
build\TeleVipNative-Preview.exe
```

脚本通过 `vswhere` 寻找 MSVC；也可在 **x64 Native Tools Command Prompt** 中运行。预览版强制使用 `TV_UI_PREVIEW=1`，无需卡密，不读取正式版设置、不连接正式版共享映射、不注入 Telegram。功能开关不执行核心操作。

可选预览环境变量：`TV_DARK=0/1`、`TV_LANG=zh/en`、`TV_PAGE=0..8`。例如：

```bat
set TV_LANG=en
set TV_DARK=0
build\TeleVipNative-Preview.exe
```

十二组测试覆盖 UI 布局、动效、识别、JSON、翻译请求、本地翻译服务、真实 CNG 签名校验，以及授权续期策略与开关恢复。测试不连接生产授权服务，不使用私人卡密。源码文件中的生产公钥用于验证固定签名测试向量，不能用于签发授权。

完整官方客户端依赖 [未公开核心](SOURCE_SCOPE.md)，公共构建的产物名称和能力与官方正式版不同。

## English

Install Visual Studio 2022 / Build Tools with **Desktop development with C++**, MSVC C++20 and a Windows SDK including C++/WinRT headers. SDK 10.0.26100.0 was used for validation. Run `build.cmd`, then `test.cmd` from the repository root. The scripts discover MSVC using `vswhere`; an x64 Native Tools prompt is also supported.

`build\TeleVipNative-Preview.exe` runs the public UI preview. It requires no license, does not load official settings or production IPC, and cannot inject Telegram. Optional environment variables select language, theme and page as shown above.

The twelve test groups run without production credentials or the private Hook. The public verification key validates a fixed CNG test vector and cannot sign licenses. Building the complete official client requires the private core and is outside this public target.


新增公开模块 / Additional public modules: `src/news*`, `src/signed_content.h`, `src/telegram_link.*`，以及缓存验签、版本解析和官方 Telegram 单实例协议回归。公告管理服务、部署配置和核心 Hook 不在此源码范围内。
The new desktop news and link modules are public. Announcement administration, deployment configuration and the Hook core remain private.
