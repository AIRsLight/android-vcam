package io.github.androidvcam.manager;

import android.os.Build;

/** Root-free local matcher for device recipes already qualified by the project. */
final class DeviceCompatibility {
    static final String NX769J_PROFILE = "nx769j-ukq1-20240417";
    static final String ONEPLUS7PRO_PROFILE = "oneplus7pro-p202303230244";
    private static final String NX769J_FINGERPRINT =
            "nubia/NX769J/NX769J:14/UKQ1.230917.001/20240417.145608:user/release-keys";
    private static final String ONEPLUS7PRO_FINGERPRINT =
            "OnePlus/OnePlus7Pro_CH/OnePlus7Pro:12/SKQ1.211113.001/P.202303230244:user/release-keys";

    static final class Profile {
        final String id;
        final String title;
        final String detail;
        final boolean qualified;

        Profile(String id, String title, String detail, boolean qualified) {
            this.id = id;
            this.title = title;
            this.detail = detail;
            this.qualified = qualified;
        }
    }

    private DeviceCompatibility() { }

    static Profile detect() {
        String fingerprint = Build.FINGERPRINT == null ? "" : Build.FINGERPRINT;
        if (ONEPLUS7PRO_FINGERPRINT.equals(fingerprint)) {
            return new Profile(ONEPLUS7PRO_PROFILE, "OnePlus 7 Pro Android 12 · 已认证",
                    "支持按应用替换相机 0/1，可使用物理相机、图片、本地视频及网络流。OEM HAL 代理只会在 Camera ABI 校验通过后挂载。",
                    true);
        }
        if (NX769J_FINGERPRINT.equals(fingerprint)) {
            return new Profile(NX769J_PROFILE, "NX769J Android 14 · 已认证",
                    "支持按应用路由公共相机 0/1；内部相机 1000/1001 已隐藏。首次启用仍由 root 模块执行一次性安全启动。",
                    true);
        }
        if (Build.VERSION.SDK_INT == 34 && "NX769J".equalsIgnoreCase(Build.DEVICE)) {
            return new Profile("nx769j-android14-candidate", "NX769J Android 14 · 构建未认证",
                    "当前系统指纹不在已验证配方中。可以读取配置，但路由模块必须保持 stock，等待重新探测 Camera ABI。",
                    false);
        }
        if (Build.VERSION.SDK_INT == 31 && "OnePlus7Pro".equalsIgnoreCase(Build.DEVICE)) {
            return new Profile("oneplus7pro-android12-candidate",
                    "OnePlus 7 Pro Android 12 · 构建未认证",
                    "当前系统指纹不在已验证配方中。可以读取配置，但 OEM HAL 代理必须保持禁用，等待重新探测 Camera ABI。",
                    false);
        }
        return new Profile("none", "当前设备尚未认证",
                "Manager 可以管理后端配置，但不会把未知设备标记为可安全启用。请先生成并验证设备配方。",
                false);
    }
}
