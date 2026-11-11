# 源码开放范围 / Source scope

TeleVip Native 采用 **公开桌面层、私有核心模块** 的结构。这个仓库中的自有源码以 MIT 许可证开放；官方发行版还包含未公开的核心实现。

| 公开源码 | 文件与范围 |
| --- | --- |
| 桌面界面与交互 | `src/app.*`、Direct2D / DirectWrite 渲染、原生窗口按钮、明暗主题、双语布局 |
| 动效系统 | `src/motion.h`、按钮悬停与按压、页面过渡、弹簧开关、减少动态效果支持 |
| 支持与社区 | 赞赏对话框、用户提供的二维码、Telegram 社区入口 |
| 通用工具与翻译 | 本地工具、翻译请求与本地翻译服务、识别与去重逻辑 |
| 授权客户端 | TLS 请求、ECDSA 验签、文件 SHA-256、DPAPI 保存、续期与断线恢复、**仅公钥** |
| 构建与测试 | 可独立编译的 UI 预览入口、开发适配层、十二组回归测试 |

**不公开：** Hook / detour 的 Rust 源码与构建产物、DLL 注入实现、目标进程内存补丁、偏移和签名表、生产共享内存 ABI、模块打包管线、服务端部署配置、数据库及任何凭据。

`src/core.h` 公开上层接口；`src/preview_platform.cpp` 明确实现公共预览目标，不连接生产共享内存，也不向目标进程注入。`src/common.h` 只保留 UI 所需标识，未提供核心实现及生产 IPC 布局。

公共构建启动为 **UI 预览**。可研究界面、主题、动效和工具源码；需要实际增强功能时，请使用 [正式发行版](https://github.com/ldxm666/TeleVip-Desktop/releases/latest)。公开源码不能独立重建完整官方二进制。详见 [构建说明](BUILDING.md) 和 [分发条款](DISTRIBUTION.md)。

---

## English

The desktop layer is public under MIT; the core module remains proprietary. Public code covers the native UI, rendering, themes, motion, localization, support dialogs, general tools, translation, client-side licensing and tests. The licensing source contains the **public verification key only**.

Private components include Hook / detour implementations, DLL injection, target memory patches, offsets and signatures, the production shared-memory ABI, module packaging, deployment configuration, databases and credentials.

`src/core.h` defines the upper-layer contract. `src/preview_platform.cpp` is an explicit preview adapter with no production IPC or injection. `build.cmd` creates a runnable UI preview; it cannot reproduce the complete official executable. Use the [stable release](https://github.com/ldxm666/TeleVip-Desktop/releases/latest) for the official client.


新增公开模块 / Additional public modules: `src/news*`, `src/signed_content.h`, `src/telegram_link.*`，以及缓存验签、版本解析和官方 Telegram 单实例协议回归。公告管理服务、部署配置和核心 Hook 不在此源码范围内。
The new desktop news and link modules are public. Announcement administration, deployment configuration and the Hook core remain private.
