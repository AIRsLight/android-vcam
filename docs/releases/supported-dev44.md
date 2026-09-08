# VCAM 0.5.0-dev.44

OnePlus Android 10–14 通用全局适配现已纳入常规 dev 发布，仍然只安装一个
`android_vcam` 模块。两个 APK 与模块版本统一为 dev.44 / versionCode 64。

## 更新内容

- 统一模块新增 `oneplus-qcom-global-shim` 配方，安装时自动选择；已有 OnePlus 7 Pro
  Android 12 / P.202303230244 和 NX769J Android 14 / 20240417.145608 精确配方优先，
  保留它们原有的按 App 路由能力。
- 其他符合条件的 OnePlus 高通 Android 10–14 设备进入实验性全局替换模式。
  需要 ARM64 `camera.qcom.so`、`local_time.default.so` 槽位和可明确识别的物理
  HIDL 2.4 `service_64`。非 OnePlus、非高通相机布局或探测不明确时拒绝安装。
- 从 init 声明读取物理相机服务名，不再固定重启名称；不修改 OEM 虚拟相机服务，
  不在设备上对系统 SO 反汇编。
- 原厂 HAL 由安装器在本机备份，同一固件升级会校验并保留原始备份，防止递归加载替换库。
  支持 MetaModule 规范化后的 vendor 路径；固件变化、损坏备份、旧覆盖仍挂载时拒绝危险升级。
- 常规发布的共享 native 组件改用 API 29 构建。管理器通过后端识别全局模式，
  复用现有图片、本地视频、彩条及 HTTP/HTTPS/HLS/RTSP 源配置与总开关。
- 增加统一安装器的选型、升级、旧模块迁移及探测失败回归测试；发行清单列出全部三个配方。

## 安装与测试边界

直接将本页的模块 ZIP 交给 KSU/APatch 安装，两个 APK 常规安装。需要已启用的兼容
MetaModule；不额外发布或安装独立通用模块。原有统一模块的同固件升级后重启即可。
如曾安装独立实验模块，先禁用并重启，再安装统一包。

通用模式只使用全局路由，不识别调用 App，已有按 App 路由不会生效。请先检查全局配置，
依次验证原厂透传、彩条、图片、本地视频及网络流，前后摄与系统/第三方相机分别记录。
物理相机互换源不属于通用模式的支持范围；升降前摄仍可能升起，物理相机占用仍可能影响会话。

**通用适配仍为实验性，不表示所有 OnePlus Android 10–14 已通过真机验证。**
本版完成构建、固件离线审查、脚本及包结构测试，没有新增真机回归结果。遇到卡顿或启动失败，
按 root 管理器的恢复方式禁用模块；关闭路由不等于卸载 HAL 覆盖，挂载失败的禁用标记
只影响下次启动，不保证当前启动自动回滚。

请通过 [Issues](https://github.com/AIRsLight/android-vcam/issues/new/choose) 提交具体设备、
ROM、Root/MetaModule 版本和检查后的诊断报告。源码从 `dev` 构建，本次不合并 `main`。

## English summary

- One unified root module now includes the experimental OnePlus Qualcomm Android 10–14
  global adapter alongside the two prioritized exact-device profiles.
- Read-only layout detection, physical-provider service discovery, device-local OEM HAL
  snapshots and protected same-firmware upgrades; no runtime disassembly.
- Shared native components target API 29. Module and both APKs use versionCode 64.
- Generic mode is global-only, not per-app routing. Offline validation is not hardware
  certification; please report device results in Issues. No automatic promotion to main.
