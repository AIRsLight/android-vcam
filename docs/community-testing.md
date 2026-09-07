# Community testing / 社区测试

Publication policy update: dev.42's separate community-kit release was withdrawn.
Future public tests use ordinary dev releases built from `dev`: one installable
module ZIP, manager APK, test APK and a manifest. The kit instructions below are
retained for local engineering use only. See [release workflow](release-workflow.md).

发布方式已调整：dev.42 独立测试套件已撤下，后续从 `dev` 分支发布常规 dev 版，
测试通过后再同步到 `main`。下面的外层测试套件说明仅供本地开发使用。

OnePlus Qualcomm Android 10–14 global replacement is experimental. The five
offline firmware samples are compatibility candidates, not device certifications.
The currently qualified exact-device release is separate from this engineering kit.

OnePlus 高通 Android 10–14 全局替换目前为实验功能。其他芯片、其他 HAL 布局以及同型号
不同 ROM 不会因为这五套固件离线通过而自动获得认证。请能够自行禁用模块并恢复系统的用户参与。

## Kit contents / 测试包内容

`android-vcam-oneplus-testkit-v0.5.0-dev.42.zip` is a desktop download bundle,
not an installable root module. Extract it first. It contains one installable
OnePlus global module ZIP, manager APK, test APK, this guide and a SHA-256 manifest.
Do not install this engineering module alongside the qualified `android_vcam`
module: disable the existing camera module and reboot before changing adapters.

外层测试包先解压，只有其中的模块 ZIP 交给 KSU/APatch 安装；两个 APK 常规安装。
管理器最低 Android 10，无需申请 root 或所有文件访问权限。模块需要当前 root 管理器支持的
已启用 MetaModule。切换已有模块前，先禁用旧相机模块并重启。

The global adapter opens the physical HAL before choosing a virtual source.
A pop-up front camera may therefore move, and physical-camera contention can
still block a session. Its post-mount disable marker applies to the next boot;
automatic current-boot rollback and boot stability are not established.
For a freeze or boot failure, use the root manager's documented recovery method
to disable the module before trying again. Turning off routes alone is not
equivalent to removing the HAL overlay.

全局原型仍会先打开物理 HAL，升降前摄可能升起，物理相机占用仍可能导致失败。
禁用标记在下一次启动生效，当前尚未验证自动回滚和启动稳定性。遇到卡顿或无法启动，先按
root 管理器的恢复方式禁用模块；仅关闭路由不会卸载 HAL 覆盖。

## Suggested sequence / 建议顺序

1. Record stock front/back camera behavior before installing. 安装前确认原厂前后摄正常。
2. Install and reboot with routes off; verify physical passthrough first. 路由关闭时先验证透传。
3. Add one global color-bar route; test front/back separately. 分别验证前后摄全局彩条。
4. Try image, local video, then HTTP/HTTPS/HLS/RTSP. Record protocol, codec,
   resolution and FPS, but do not post private URLs. 按顺序测试其他源，记录格式和规格。
5. Compare the test app's Camera1/Camera2/NDK paths and ordinary third-party apps.
   Record the app/version; include the stock camera separately. 记录验证 App、第三方与系统相机结果。
6. Record camera reopen, source switching, reboot and module-disable recovery.
   项目填通过、失败或未测，不要将未测项默认为通过。

Changing routing normally requires reopening the camera session. Reinstalling
the manager does not reset backend configuration. Old per-app routes are ignored
by this global-only adapter; review existing global routes before testing.

## Report results / 提交结果

Manager → System details → Diagnostic report → Save report.
管理器 → 系统详情 → 诊断报告 → 保存报告。

Reports include ROM fingerprint, version, cached adapter capability fields and
mount/enable markers. They exclude media, source URLs and configured app routes.
The report is not uploaded automatically. Review the text and attach it to an
[Issue](https://github.com/AIRsLight/android-vcam/issues/new/choose).
When the daemon is unavailable or too old, the manager exports local device
information with `backend=unavailable` or `diagnostics_unsupported_or_failed`.

报告包含 ROM 指纹、版本及缓存状态。报告不会自动上传，请检查后自行附在 Issue 中。
后端不可用或版本过旧也能导出设备信息；无法进入系统时，在 Issue 中说明即可，无需强行导出。

Use the bug template for failures and the compatibility template for full or
partial success. Include exact model/SKU, chipset, ROM build, KSU/APatch and
MetaModule versions, reproduction steps and behavior after disabling the module.
Do not post raw full logcat, private video addresses, passwords or personal frames.

Community observations are recorded per device and ROM as **reported**, including
both successes and failures. Promotion to **qualified** requires reviewed,
reproducible runtime evidence. No community reports have been received as part of
this checkpoint, and this kit itself has not been run on a phone.

## Rebuild / 重新构建

```powershell
pwsh -File tools/package-oneplus-testkit.ps1
```

This builds the ARM64 native payload with API 29, both APKs with minimum API 29,
the module ZIP and a versioned bundle. Local build and offline checks do not
replace the user-reported device tests above.
