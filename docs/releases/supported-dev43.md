# VCAM 0.5.0-dev.43

从 `dev` 分支生成的常规测试版。提供一个自动识别设备的模块 ZIP、管理器 APK、验证 App APK
及 SHA-256 清单，无需解压外层测试套件。测试结果请通过
[GitHub Issues](https://github.com/AIRsLight/android-vcam/issues/new/choose) 反馈。

## 更新内容

- 管理器最低安装版本调整为 Android 10；模块与两个 APK 统一为 dev.43，versionCode 63，
  可以覆盖安装旧版 APK，包括此前的 dev.42。
- 新增“系统详情 → 诊断报告”：预览并保存设备、模块版本和缓存适配状态，管理器无需 root。
  后端不可用或版本过旧时仍可导出本机信息。报告不会自动上传，也不收集源地址、媒体或应用路由名单。
- 更新 HTTPS/HLS 支持和证书处理，减少对 ROM 内置 curl 等工具的依赖。
- 扩充设备能力探测：识别相机服务与 HAL 位数、混合 Provider、OEM 虚拟 Provider 声明及
  OnePlus 全局适配候选状态。独立 OEM 虚拟 Provider 不再仅因存在而被当作冲突。
- 增加中英双语故障报告和兼容性测试 Issue 模板，区分通过、失败和未测项目。
- 发布流程切换为 `dev` 构建测试版，测试通过后再同步 main；发布前校验分支、提交和产物哈希。

## 本次安装范围

统一模块仍只接入以下两个已有配方，安装时检查指定固件身份：

| 设备 | 固件 | Root 管理器 |
| --- | --- | --- |
| OnePlus 7 Pro | Android 12 / P.202303230244 | APatch + 已启用的兼容 MetaModule |
| NX769J | Android 14 / UKQ1 / 20240417.145608 | KernelSU + 已启用的兼容 MetaModule |

**OnePlus Android 10–14 通用全局适配尚未接入本次统一安装包。** 它的代码、五套固件离线分析
及快照保护继续在 dev 开发，其他固件仍会拒绝安装。管理器支持 Android 10 不等于模块已经
适配所有 Android 10 设备。此前独立 dev.42 测试套件已经撤回。

## 安装与反馈

将 `android-vcam-module-v0.5.0-dev.43.zip` 直接交给 KSU/APatch 安装，两个 APK 常规安装。
若仍启用了旧的独立实验模块，先禁用并重启，再切换到统一模块，避免相机覆盖同时生效。
升级模块后重启；修改路由后重新打开相机会话。

本次完成构建、离线测试及包检查，**没有新增本版真机回归结果**。请在 Issue 中提供具体型号、
ROM、Root/MetaModule 版本、源类型、目标 App、复现步骤和检查后的诊断报告。
不要提交私人源地址、密码或个人画面。main 不随本次测试版发布自动更新。

## English summary

- Normal dev-branch pre-release: one auto-selecting root module and two APKs.
- Android 10+ manager, root-free diagnostic export, HTTPS/HLS and capability-discovery updates.
- Module and APK versionCode is 63; previous dev.42 APKs can be upgraded.
- Installation remains limited to the two exact OnePlus 7 Pro Android 12 and NX769J Android 14
  profiles above. The generic OnePlus Android 10–14 adapter is not in this module yet.
- Build/offline verification only for this release; please report device results in Issues.
