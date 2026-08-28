package io.github.androidvcam.manager;

import android.content.Context;
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

    static Profile detect(Context context) {
        String fingerprint = Build.FINGERPRINT == null ? "" : Build.FINGERPRINT;
        if (ONEPLUS7PRO_FINGERPRINT.equals(fingerprint)) {
            return new Profile(ONEPLUS7PRO_PROFILE,
                    context.getString(R.string.profile_oneplus_qualified_title),
                    context.getString(R.string.profile_oneplus_qualified_detail),
                    true);
        }
        if (NX769J_FINGERPRINT.equals(fingerprint)) {
            return new Profile(NX769J_PROFILE,
                    context.getString(R.string.profile_nx_qualified_title),
                    context.getString(R.string.profile_nx_qualified_detail),
                    true);
        }
        if (Build.VERSION.SDK_INT == 34 && "NX769J".equalsIgnoreCase(Build.DEVICE)) {
            return new Profile("nx769j-android14-candidate",
                    context.getString(R.string.profile_nx_candidate_title),
                    context.getString(R.string.profile_nx_candidate_detail),
                    false);
        }
        if (Build.VERSION.SDK_INT == 31 && "OnePlus7Pro".equalsIgnoreCase(Build.DEVICE)) {
            return new Profile("oneplus7pro-android12-candidate",
                    context.getString(R.string.profile_oneplus_candidate_title),
                    context.getString(R.string.profile_oneplus_candidate_detail),
                    false);
        }
        return new Profile("none", context.getString(R.string.profile_unknown_title),
                context.getString(R.string.profile_unknown_detail),
                false);
    }
}
