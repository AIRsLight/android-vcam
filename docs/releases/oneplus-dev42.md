# OnePlus community test kit · 0.5.0-dev.42

**Withdrawn on 2026-09-07.** GitHub release and uploaded assets removed at the
user's request. The source tag and local artifacts remain for audit. This page
is historical; follow the [dev release workflow](../release-workflow.md).

Experimental **OnePlus Qualcomm Android 10–14 global camera replacement**.
This is a community test build, not a claim that all OnePlus devices are supported.
The existing dev.39 qualified release remains available for its exact-device profiles.

## Download and install / 下载与安装

Download the **testkit ZIP**, extract it, and read `TESTING.md`.
Only the inner `android-vcam-oneplus-global-v0.5.0-dev.42.zip` is installed in
KSU/APatch. Install the two APKs normally. `manifest.json` records SHA-256 hashes
of the inner artifacts. The kit contains one root module; do not flash the outer ZIP.

下载测试包后先解压，阅读 `TESTING.md`。其中只有 OnePlus 模块 ZIP 交给 KSU/APatch
安装，两个 APK 常规安装。需要启用 root 管理器支持的 MetaModule。切换已有相机模块前，
先禁用旧模块并重启。该包仅面向 OnePlus 高通全局适配测试，不用于 NX769J。

## Changes / 改动

- Common API 29 ARM64 Camera HAL adapter; original HAL snapshot stays on the phone.
- Manager now installs on Android 10/11 as well as 12–14.
- System details → Diagnostic report: preview and save device/adapter status
  without root permission, even when the backend is unavailable.
- Reports exclude source URLs, media and app route lists; nothing is uploaded automatically.
- Upgrade and OTA snapshot integrity guards; actual ELF export checks in packaging.
- [Bug and compatibility forms](https://github.com/AIRsLight/android-vcam/issues/new/choose)
  for user-reported runtime results. English and Chinese are welcome.

## Evidence and limits / 验证与限制

32 offline tests, native/APK builds and archive checks passed. Five firmware
samples have candidates for all six direct libraries and 107 strong imports.
Actual linker namespace loading, frame delivery, GUI operation, boot and recovery
have **not** been verified on phones with this build.

The adapter still opens the physical camera first: pop-up movement and contention
can occur. A post-mount disable marker affects the next boot, not current-boot
rollback. Participants need a working method to disable modules and recover
their own device. Record pass/fail/not-tested per camera, source and target app;
successful reports remain observations until reviewed.

实际出帧、界面操作、启动与恢复由用户测试并报告 Issue；未测项目请填写“未测”。
报告提交前请检查内容，不要附私人视频源、密码、完整系统日志或个人画面。

[Testing guide](https://github.com/AIRsLight/android-vcam/blob/main/docs/community-testing.md)
