package io.github.androidvcam.manager;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.SurfaceTexture;
import android.graphics.Typeface;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.graphics.drawable.ColorDrawable;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CameraManager;
import android.hardware.camera2.params.StreamConfigurationMap;
import android.media.MediaMetadataRetriever;
import android.net.Uri;
import android.os.Bundle;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.provider.OpenableColumns;
import android.content.res.ColorStateList;
import android.text.Editable;
import android.text.TextUtils;
import android.text.TextWatcher;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.AdapterView;
import android.widget.BaseAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.Spinner;
import android.widget.ArrayAdapter;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.TreeSet;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class MainActivity extends Activity {
    private static final int REQUEST_IMAGE = 4101;
    private static final int REQUEST_VIDEO = 4102;
    private static final int FRAME_WIDTH = 576;
    private static final int FRAME_HEIGHT = 324;
    private static final String[] TYPE_LABELS = {
            "内置彩条", "静态图片", "设备本地视频", "HTTPS 视频文件",
            "HTTP 视频 / 流", "HLS（HTTP）", "RTSP"
    };
    private static final String[] TYPE_CODES = {
            "pattern", "image", "video", "https", "http", "hls", "rtsp"
    };

    private final ExecutorService worker = Executors.newFixedThreadPool(3);
    private final Handler main = new Handler(Looper.getMainLooper());
    private final List<Provider> providers = new ArrayList<>();
    private final List<AppEntry> allApps = new ArrayList<>();
    private final Map<String, String> routes = new HashMap<>();

    private FrameLayout content;
    private View statusPage;
    private View sourcesPage;
    private View appsPage;
    private TextView statusText;
    private TextView statusDetail;
    private TextView providerCountText;
    private TextView routeCountText;
    private TextView halText;
    private LinearLayout providerList;
    private LinearLayout routeList;
    private TextView providerRefreshText;
    private TextView routeRefreshText;
    private final List<TextView> navigationItems = new ArrayList<>();
    private final Map<String, Drawable> appIcons = new HashMap<>();
    private AddProviderSession activeAddSession;
    private boolean providerRefreshInFlight;
    private boolean providerRefreshPending;
    private boolean routeRefreshInFlight;
    private boolean routeRefreshPending;
    private boolean appsDirty = true;
    private long lastProviderRefresh;
    private long lastAppRefresh;
    private long lastRouteRefresh;
    private final BroadcastReceiver packageReceiver = new BroadcastReceiver() {
        @Override public void onReceive(Context context, Intent intent) {
            appsDirty = true;
            String packageName = intent.getData() == null ? null
                    : intent.getData().getSchemeSpecificPart();
            if (packageName != null) {
                appIcons.remove(packageName);
                for (int index = allApps.size() - 1; index >= 0; --index) {
                    if (packageName.equals(allApps.get(index).packageName)) allApps.remove(index);
                }
                if (!Intent.ACTION_PACKAGE_REMOVED.equals(intent.getAction())) {
                    AppEntry restored = installedApp(packageName);
                    if (restored != null) allApps.add(restored);
                }
            }
            if (appsPage != null && appsPage.getVisibility() == View.VISIBLE) renderRoutes();
        }
    };
    private final TargetCameraSpec[] targetCameras = {
            new TargetCameraSpec("0", 3000, 4000, 90),
            new TargetCameraSpec("1", 3000, 4000, 270)
    };
    private int frameSequence = (int) (System.currentTimeMillis() / 1000);

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        loadTargetCameraSpecs();
        setContentView(buildRoot());
        IntentFilter packageFilter = new IntentFilter();
        packageFilter.addAction(Intent.ACTION_PACKAGE_ADDED);
        packageFilter.addAction(Intent.ACTION_PACKAGE_REMOVED);
        packageFilter.addAction(Intent.ACTION_PACKAGE_CHANGED);
        packageFilter.addDataScheme("package");
        if (Build.VERSION.SDK_INT >= 33) {
            registerReceiver(packageReceiver, packageFilter, Context.RECEIVER_NOT_EXPORTED);
        } else {
            registerReceiver(packageReceiver, packageFilter);
        }
        showPage(statusPage);
        refreshStatus();
    }

    private View buildRoot() {
        LinearLayout root = vertical();
        root.setBackgroundColor(0xfff6f7fb);

        LinearLayout appBar = horizontal();
        appBar.setGravity(Gravity.CENTER_VERTICAL);
        appBar.setPadding(dp(20), dp(12), dp(20), dp(12));
        appBar.setBackgroundColor(Color.WHITE);
        appBar.setElevation(dp(2));
        TextView mark = text("V", 20, Color.WHITE);
        mark.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        mark.setGravity(Gravity.CENTER);
        mark.setBackground(roundGradient(0xff2563eb, 0xff4f46e5, 14));
        appBar.addView(mark, new LinearLayout.LayoutParams(dp(44), dp(44)));
        LinearLayout brand = vertical();
        TextView brandTitle = text("VCAM", 20, 0xff111827);
        brandTitle.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        brand.addView(brandTitle);
        brand.addView(text("虚拟摄像头控制中心", 11, 0xff64748b));
        appBar.addView(brand, weightedMargins(12, 0, 0, 0));
        TextView localBadge = pill("本机后端", 0xffeff6ff, 0xff2563eb);
        appBar.addView(localBadge);
        root.addView(appBar, matchWrap());

        content = new FrameLayout(this);
        root.addView(content, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1));
        statusPage = buildStatusPage();
        sourcesPage = buildSourcesPage();
        appsPage = buildAppsPage();
        content.addView(statusPage, matchMatch());
        content.addView(sourcesPage, matchMatch());
        content.addView(appsPage, matchMatch());

        LinearLayout navigation = horizontal();
        navigation.setGravity(Gravity.CENTER);
        navigation.setPadding(dp(10), dp(7), dp(10), dp(8));
        navigation.setBackgroundColor(Color.WHITE);
        navigation.setElevation(dp(12));
        TextView statusTab = navigationItem("概览", "●");
        TextView sourcesTab = navigationItem("视频源", "◆");
        TextView appsTab = navigationItem("路由", "■");
        navigation.addView(statusTab, weightedMargins(3, 0, 3, 0));
        navigation.addView(sourcesTab, weightedMargins(3, 0, 3, 0));
        navigation.addView(appsTab, weightedMargins(3, 0, 3, 0));
        root.addView(navigation, matchWrap());

        statusTab.setOnClickListener(view -> { showPage(statusPage); refreshStatus(); });
        sourcesTab.setOnClickListener(view -> { showPage(sourcesPage); refreshProviders(false); });
        appsTab.setOnClickListener(view -> { showPage(appsPage); refreshRoutes(false); });
        return root;
    }

    private View buildStatusPage() {
        ScrollView scroll = new ScrollView(this);
        LinearLayout body = pageBody();
        body.addView(pageTitle("系统概览", "模块健康度与当前替换资源"));

        LinearLayout hero = vertical();
        hero.setPadding(dp(20), dp(18), dp(20), dp(18));
        hero.setBackground(roundGradient(0xff1d4ed8, 0xff4f46e5, 22));
        hero.setElevation(dp(3));
        TextView overline = text("VCAM SERVICE", 11, 0xffbfdbfe);
        overline.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        hero.addView(overline);
        statusText = text("正在连接后端…", 23, Color.WHITE);
        statusText.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        hero.addView(statusText, matchWrapMargins(0, 5, 0, 0));
        statusDetail = text("正在读取模块状态", 13, 0xffdbeafe);
        hero.addView(statusDetail, matchWrapMargins(0, 7, 0, 0));
        body.addView(hero, matchWrapMargins(0, 18, 0, 0));

        LinearLayout metrics = horizontal();
        providerCountText = metricCard("视频源", "—", 0xffeff6ff, 0xff2563eb);
        routeCountText = metricCard("替换规则", "—", 0xfff5f3ff, 0xff7c3aed);
        metrics.addView((View) providerCountText.getTag(), weightedMargins(0, 0, 7, 0));
        metrics.addView((View) routeCountText.getTag(), weightedMargins(7, 0, 0, 0));
        body.addView(metrics, matchWrapMargins(0, 14, 0, 0));

        LinearLayout details = card();
        details.addView(cardHeading("HAL 指纹", "当前挂载的相机代理版本"));
        halText = text("读取中…", 12, 0xff475569);
        halText.setTextIsSelectable(true);
        halText.setTypeface(Typeface.MONOSPACE);
        details.addView(halText, matchWrapMargins(0, 10, 0, 0));
        body.addView(details, matchWrapMargins(0, 14, 0, 0));

        TextView refresh = primaryButton("刷新状态");
        refresh.setOnClickListener(view -> refreshStatus());
        body.addView(refresh, matchWrapMargins(0, 14, 0, 0));
        LinearLayout privacy = card();
        privacy.addView(cardHeading("无需 Root 授权", "管理器仅通过受鉴权的本地后端修改配置"));
        TextView note = text(
                "管理器不申请 root 权限，只连接模块内受鉴权的本地后端。配置和媒体由后端持久保存，卸载管理器也不会停止已配置的视频源与替换规则。",
                12, 0xff64748b);
        privacy.addView(note, matchWrapMargins(0, 8, 0, 0));
        body.addView(privacy, matchWrapMargins(0, 14, 0, 0));
        scroll.addView(body);
        return scroll;
    }

    private View buildSourcesPage() {
        ScrollView scroll = new ScrollView(this);
        LinearLayout body = pageBody();
        LinearLayout titleRow = horizontal();
        titleRow.setGravity(Gravity.CENTER_VERTICAL);
        titleRow.addView(pageTitle("视频源", "管理物理、图片与网络输入"), weighted());
        TextView add = compactPrimaryButton("＋ 添加");
        add.setOnClickListener(view -> showAddProviderDialog());
        titleRow.addView(add);
        body.addView(titleRow, matchWrap());
        body.addView(text("每个来源可独立设置两个目标相机的取景与方向。",
                12, 0xff64748b), matchWrapMargins(0, 8, 0, 12));
        LinearLayout refreshRow = horizontal();
        refreshRow.setGravity(Gravity.CENTER_VERTICAL);
        providerRefreshText = text("尚未读取", 11, 0xff64748b);
        refreshRow.addView(providerRefreshText, weighted());
        TextView refresh = compactSecondaryButton("刷新");
        refresh.setOnClickListener(view -> refreshProviders(true));
        refreshRow.addView(refresh);
        body.addView(refreshRow, matchWrapMargins(0, 0, 0, 10));
        providerList = vertical();
        body.addView(providerList, matchWrap());
        scroll.addView(body);
        return scroll;
    }

    private View buildAppsPage() {
        ScrollView scroll = new ScrollView(this);
        LinearLayout body = pageBody();
        LinearLayout titleRow = horizontal();
        titleRow.setGravity(Gravity.CENTER_VERTICAL);
        titleRow.addView(pageTitle("应用路由", "仅显示已配置的应用及双相机来源"), weighted());
        TextView add = compactPrimaryButton("＋ 新增");
        add.setOnClickListener(view -> showAddRouteDialog());
        titleRow.addView(add);
        body.addView(titleRow, matchWrap());
        body.addView(text("卸载应用不会删除路由；重新安装同一包名后会自动恢复。",
                12, 0xff64748b), matchWrapMargins(0, 8, 0, 10));
        LinearLayout refreshRow = horizontal();
        refreshRow.setGravity(Gravity.CENTER_VERTICAL);
        routeRefreshText = text("尚未读取路由", 11, 0xff64748b);
        refreshRow.addView(routeRefreshText, weighted());
        TextView refresh = compactSecondaryButton("刷新");
        refresh.setOnClickListener(view -> refreshRoutes(true));
        refreshRow.addView(refresh);
        body.addView(refreshRow, matchWrapMargins(0, 0, 0, 10));
        routeList = vertical();
        body.addView(routeList, matchWrap());
        scroll.addView(body);
        return scroll;
    }

    private void refreshStatus() {
        statusText.setText("正在检查服务…");
        statusDetail.setText("正在读取模块状态");
        runAsync(() -> BackendClient.controller("status"), result -> {
            result.requireSuccess();
            Map<String, String> values = parseProperties(result.output);
            boolean healthy = "true".equals(values.get("module_enabled")) &&
                    "true".equals(values.get("mount_active"));
            statusText.setText(healthy ? "服务运行正常" : "服务需要检查");
            statusDetail.setText("模块" + yesNo(values.get("module_enabled")) +
                    "  ·  挂载" + yesNo(values.get("mount_active")) +
                    "  ·  相机设备 " + friendlyCount(values.get("camera_devices")));
            providerCountText.setText(values.getOrDefault("provider_count", "0"));
            routeCountText.setText(values.getOrDefault("route_count", "0"));
            halText.setText(values.getOrDefault("hal_hash", "未知"));
        });
    }

    private void refreshProviders() { refreshProviders(true); }

    private void refreshProviders(boolean force) {
        if (!providers.isEmpty()) renderProviders();
        long age = SystemClock.elapsedRealtime() - lastProviderRefresh;
        if (!force && !providers.isEmpty() && age < 5_000) return;
        if (providerRefreshInFlight) {
            providerRefreshPending |= force;
            return;
        }
        providerRefreshInFlight = true;
        providerRefreshText.setText(providers.isEmpty() ? "正在读取视频源…" : "正在后台更新…");
        if (providers.isEmpty()) {
            providerList.removeAllViews();
            providerList.addView(text("正在读取提供器…", 14, 0xff667383));
        }
        runAsync(() -> BackendClient.controller("providers"), result -> {
            result.requireSuccess();
            providers.clear();
            providers.addAll(parseProviders(result.output));
            lastProviderRefresh = SystemClock.elapsedRealtime();
            providerRefreshInFlight = false;
            providerRefreshText.setText("刚刚更新 · " + providers.size() + " 个来源");
            renderProviders();
            if (providerRefreshPending) {
                providerRefreshPending = false;
                refreshProviders(true);
            }
        }, error -> {
            providerRefreshInFlight = false;
            providerRefreshText.setText("更新失败，保留现有列表");
            showError(error);
        });
    }

    private void renderProviders() {
        providerList.removeAllViews();
        for (Provider provider : providers) {
            LinearLayout providerCard = card();
            LinearLayout top = horizontal();
            top.setGravity(Gravity.CENTER_VERTICAL);
            TextView typeIcon = text(providerSymbol(provider.type), 17,
                    providerAccent(provider.type));
            typeIcon.setGravity(Gravity.CENTER);
            typeIcon.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
            typeIcon.setBackground(roundRect(providerTint(provider.type), 14));
            top.addView(typeIcon, new LinearLayout.LayoutParams(dp(44), dp(44)));
            LinearLayout identity = vertical();
            TextView name = text(provider.name, 16, 0xff111827);
            name.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
            identity.addView(name);
            identity.addView(text(typeName(provider.type) + "  ·  " + provider.id,
                    11, 0xff64748b));
            top.addView(identity, weightedMargins(12, 0, 8, 0));
            top.addView(pill(provider.running ? "运行中" : "已停止",
                    provider.running ? 0xffecfdf5 : 0xfff1f5f9,
                    provider.running ? 0xff059669 : 0xff64748b));
            providerCard.addView(top);
            if (!provider.source.isEmpty()) {
                TextView source = text(provider.source, 12, 0xff475569);
                source.setMaxLines(2);
                source.setPadding(dp(12), dp(9), dp(12), dp(9));
                source.setBackground(roundRect(0xfff8fafc, 10));
                providerCard.addView(source, matchWrapMargins(0, 12, 0, 0));
            }
            if (provider.removable) {
                LinearLayout primaryActions = horizontal();
                TextView preview = secondaryButton("预览");
                preview.setOnClickListener(view -> showProviderPreview(provider));
                TextView edit = secondaryButton("编辑");
                edit.setOnClickListener(view -> showProviderDialog(provider));
                primaryActions.addView(preview, weighted());
                primaryActions.addView(edit, weightedMargins(8, 0, 0, 0));
                providerCard.addView(primaryActions, matchWrapMargins(0, 12, 0, 0));

                LinearLayout actions = horizontal();
                TextView toggle = secondaryButton(provider.running ? "停止" : "启动");
                toggle.setOnClickListener(view -> runProviderAction(
                        provider.running ? "provider-stop" : "provider-start", provider.id));
                TextView remove = dangerButton("删除");
                remove.setOnClickListener(view -> confirmRemove(provider));
                actions.addView(toggle, weighted());
                actions.addView(remove, weightedMargins(8, 0, 0, 0));
                providerCard.addView(actions, matchWrapMargins(0, 8, 0, 0));
            }
            providerList.addView(providerCard, matchWrapMargins(0, 0, 0, 12));
        }
    }

    private void showAddProviderDialog() {
        showProviderDialog(null);
    }

    private void showProviderDialog(Provider existing) {
        LinearLayout form = dialogForm();
        EditText name = input("名称，例如：会议背景");
        Spinner type = new Spinner(this);
        type.setAdapter(new ArrayAdapter<>(this,
                android.R.layout.simple_spinner_dropdown_item, TYPE_LABELS));
        EditText source = input("网络地址");
        EditText fps = numericInput("1–60", existing == null ? "30" : Integer.toString(existing.fps));
        EditText maxWidth = numericInput("160–1920", existing == null ? "1280" : Integer.toString(existing.maxWidth));
        EditText maxHeight = numericInput("120–1920", existing == null ? "720" : Integer.toString(existing.maxHeight));
        form.addView(label("名称")); form.addView(name);
        form.addView(label("类型")); form.addView(type);
        LinearLayout sourceBlock = vertical();
        TextView sourceLabel = label("网络地址");
        sourceBlock.addView(sourceLabel); sourceBlock.addView(source);
        form.addView(sourceBlock);
        LinearLayout fileBlock = vertical();
        fileBlock.addView(label("本地文件"));
        Button choose = secondaryButton("立即选择文件");
        TextView selectedFile = text("尚未选择文件", 12, 0xff64748b);
        fileBlock.addView(choose);
        fileBlock.addView(selectedFile, matchWrapMargins(0, 7, 0, 0));
        form.addView(fileBlock);
        form.addView(label("源解码帧率（fps）")); form.addView(fps);
        LinearLayout dimensions = horizontal();
        dimensions.addView(maxWidth, weighted()); dimensions.addView(maxHeight, weighted());
        form.addView(label("源解码上限（宽 × 高，保持原始比例）")); form.addView(dimensions);
        LinearLayout progressRow = horizontal();
        progressRow.setGravity(Gravity.CENTER_VERTICAL);
        ProgressBar progress = new ProgressBar(this);
        progressRow.addView(progress, new LinearLayout.LayoutParams(dp(28), dp(28)));
        TextView progressText = text("正在连接并读取预览…", 12, 0xff2563eb);
        progressRow.addView(progressText, weightedMargins(10, 0, 0, 0));
        progressRow.setVisibility(View.GONE);
        form.addView(progressRow, matchWrapMargins(0, 14, 0, 0));
        TextView errorText = text("", 12, 0xffdc2626);
        errorText.setVisibility(View.GONE);
        form.addView(errorText, matchWrapMargins(0, 8, 0, 0));

        ScrollView scroll = new ScrollView(this);
        scroll.addView(form);
        AddProviderSession session = new AddProviderSession(existing, name, type, source,
                sourceLabel, sourceBlock, fileBlock, choose, selectedFile, fps, maxWidth,
                maxHeight, progressRow, progressText, errorText);
        if (existing != null) {
            name.setText(existing.name);
            source.setText(existing.source);
            type.setSelection(typeIndex(existing.type));
            type.setEnabled(false);
            session.lastType = existing.type;
            session.urls.put(existing.type, existing.source);
        }
        AlertDialog dialog = new AlertDialog.Builder(this)
                .setTitle(existing == null ? "添加视频源" : "编辑视频源")
                .setView(scroll)
                .setNegativeButton("取消", null)
                .setPositiveButton(existing == null ? "继续" : "测试并继续", null)
                .create();
        session.dialog = dialog;
        type.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
                updateProviderFormType(session);
            }
            @Override public void onNothingSelected(AdapterView<?> parent) { }
        });
        choose.setOnClickListener(view -> {
            String code = TYPE_CODES[type.getSelectedItemPosition()];
            activeAddSession = session;
            chooseFile("image".equals(code) ? "image/*" : "video/*",
                    "image".equals(code) ? REQUEST_IMAGE : REQUEST_VIDEO);
        });
        dialog.setOnDismissListener(ignored -> {
            if (activeAddSession == session) activeAddSession = null;
        });
        dialog.setOnShowListener(ignored -> {
            dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(view ->
                    continueProviderForm(session));
            updateProviderFormType(session);
        });
        activeAddSession = session;
        dialog.show();
    }

    private void updateProviderFormType(AddProviderSession session) {
        String code = TYPE_CODES[session.type.getSelectedItemPosition()];
        if (isNetworkType(session.lastType)) {
            session.urls.put(session.lastType, session.source.getText().toString().trim());
        }
        session.lastType = code;
        boolean network = isNetworkType(code);
        boolean local = "image".equals(code) || "video".equals(code);
        session.sourceBlock.setVisibility(network ? View.VISIBLE : View.GONE);
        session.fileBlock.setVisibility(local ? View.VISIBLE : View.GONE);
        if (network) {
            String value = session.urls.get(code);
            if (value == null || value.isEmpty()) value = protocolPrefix(code);
            session.source.setText(value);
            session.source.setSelection(session.source.length());
            session.sourceLabel.setText("网络地址（" + protocolPrefix(code) + "…）");
        }
        if (local) {
            if (session.existing != null) {
                session.chooseFile.setVisibility(View.GONE);
                session.selectedFile.setText("沿用已导入的本地媒体；名称和取景参数仍可编辑");
            } else {
                session.chooseFile.setVisibility(View.VISIBLE);
                Uri chosen = session.files.get(code);
                session.selectedFile.setText(chosen == null ? "尚未选择文件" : displayName(chosen));
            }
        }
        session.errorText.setVisibility(View.GONE);
    }

    private void continueProviderForm(AddProviderSession session) {
        if (session.busy) return;
        String code = TYPE_CODES[session.type.getSelectedItemPosition()];
        String displayName = session.name.getText().toString().trim();
        if (displayName.isEmpty()) displayName = TYPE_LABELS[session.type.getSelectedItemPosition()];
        String source = isNetworkType(code) ? session.source.getText().toString().trim()
                : session.existing == null ? "" : session.existing.source;
        if (isNetworkType(code) && !source.startsWith(protocolPrefix(code))) {
            showFormError(session, "地址必须以 " + protocolPrefix(code) + " 开头");
            return;
        }
        Uri uri = session.files.get(code);
        if (session.existing == null && ("image".equals(code) || "video".equals(code)) && uri == null) {
            showFormError(session, "请先选择本地文件，再继续");
            return;
        }
        PendingProvider pending = new PendingProvider(
                session.existing == null ? "p-" + Long.toString(System.currentTimeMillis(), 36)
                        : session.existing.id,
                code, displayName, source,
                parseBounded(session.fps, 1, 60, 30),
                parseBounded(session.maxWidth, 160, 1920, 1280),
                parseBounded(session.maxHeight, 120, 1920, 720));
        pending.uri = uri;
        pending.editing = session.existing != null;
        if (session.existing != null) {
            pending.view0 = session.existing.view0;
            pending.view1 = session.existing.view1;
        }
        setProviderFormBusy(session, true,
                isNetworkType(code) ? "正在连接并读取预览，慢速源可能需要 15–30 秒…" : "正在读取源预览…");
        runAsync(() -> new PreviewResult(loadPreview(pending)), result -> {
            if (!session.dialog.isShowing()) return;
            session.dialog.dismiss();
            showTransformEditor(pending, ((PreviewResult) result).bitmap);
        }, error -> {
            if (!session.dialog.isShowing()) return;
            setProviderFormBusy(session, false, "");
            showFormError(session, friendlyError(error));
        });
    }

    private void setProviderFormBusy(AddProviderSession session, boolean busy, String message) {
        session.busy = busy;
        session.progressText.setText(message);
        session.progressRow.setVisibility(busy ? View.VISIBLE : View.GONE);
        session.dialog.getButton(AlertDialog.BUTTON_POSITIVE).setEnabled(!busy);
        session.dialog.getButton(AlertDialog.BUTTON_NEGATIVE).setEnabled(!busy);
        session.type.setEnabled(!busy && session.existing == null);
        session.name.setEnabled(!busy); session.source.setEnabled(!busy);
        session.fps.setEnabled(!busy); session.maxWidth.setEnabled(!busy);
        session.maxHeight.setEnabled(!busy); session.chooseFile.setEnabled(!busy);
        if (busy) session.errorText.setVisibility(View.GONE);
    }

    private void showFormError(AddProviderSession session, String message) {
        session.errorText.setText(message);
        session.errorText.setVisibility(View.VISIBLE);
    }

    private void chooseFile(String mime, int request) {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType(mime);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION |
                Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        startActivityForResult(intent, request);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (resultCode != RESULT_OK || data == null || data.getData() == null ||
                activeAddSession == null) return;
        Uri uri = data.getData();
        try {
            getContentResolver().takePersistableUriPermission(uri,
                    data.getFlags() & Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (Exception ignored) { }
        String code = requestCode == REQUEST_IMAGE ? "image" : "video";
        activeAddSession.files.put(code, uri);
        activeAddSession.selectedFile.setText(displayName(uri));
        activeAddSession.errorText.setVisibility(View.GONE);
    }

    private Bitmap loadPreview(PendingProvider pending) throws IOException {
        if ("pattern".equals(pending.type)) {
            return buildPatternPreview();
        }
        if ("image".equals(pending.type)) {
            if (pending.editing && pending.uri == null) return loadBackendProviderPreview(pending.id);
            try (InputStream input = getContentResolver().openInputStream(pending.uri)) {
                Bitmap bitmap = BitmapFactory.decodeStream(input);
                if (bitmap == null) throw new IOException("无法解码所选图片");
                return bitmap;
            }
        }
        if (isNetworkType(pending.type)) {
            return loadBackendNetworkPreview(pending);
        }
        if (pending.editing && pending.uri == null) return loadBackendProviderPreview(pending.id);
        MediaMetadataRetriever retriever = new MediaMetadataRetriever();
        try {
            if (pending.uri != null) retriever.setDataSource(this, pending.uri);
            else retriever.setDataSource(pending.source, new HashMap<>());
            Bitmap bitmap = retriever.getFrameAtTime(0, MediaMetadataRetriever.OPTION_CLOSEST_SYNC);
            if (bitmap == null) throw new IOException("视频源没有可读取的预览帧");
            return bitmap;
        } catch (RuntimeException error) {
            throw new IOException("无法读取视频源预览，请检查地址或媒体格式", error);
        } finally { retriever.release(); }
    }

    private Bitmap loadBackendNetworkPreview(PendingProvider pending) throws IOException {
        BackendClient.Result preview = BackendClient.controller(
                "source-preview", pending.type, BackendClient.encode(pending.source));
        preview.requireSuccess();
        return decodeBackendFrame(preview.output);
    }

    private Bitmap loadBackendProviderPreview(String id) throws IOException {
        BackendClient.Result preview = BackendClient.controller("provider-frame", id);
        preview.requireSuccess();
        return decodeBackendFrame(preview.output);
    }

    private Bitmap buildPatternPreview() {
        Bitmap bitmap = Bitmap.createBitmap(640, 360, Bitmap.Config.ARGB_8888);
        Canvas canvas = new Canvas(bitmap);
        int[] colors = {Color.WHITE, Color.YELLOW, Color.CYAN, Color.GREEN,
                Color.MAGENTA, Color.RED, Color.BLUE, Color.BLACK};
        for (int i = 0; i < colors.length; ++i) {
            canvas.drawRect(i * 80, 0, (i + 1) * 80, 360, colorPaint(colors[i]));
        }
        return bitmap;
    }

    private void showProviderPreview(Provider provider) {
        LinearLayout body = dialogForm();
        TextView description = text("显示后端当前实际发布的画面，不改变来源配置。",
                12, 0xff64748b);
        body.addView(description);

        FrameLayout frame = new FrameLayout(this);
        frame.setBackground(roundRect(0xff0f172a, 14));
        ImageView image = new ImageView(this);
        image.setScaleType(ImageView.ScaleType.FIT_CENTER);
        image.setAdjustViewBounds(true);
        frame.addView(image, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        ProgressBar progress = new ProgressBar(this);
        FrameLayout.LayoutParams progressParams = new FrameLayout.LayoutParams(dp(38), dp(38));
        progressParams.gravity = Gravity.CENTER;
        frame.addView(progress, progressParams);
        body.addView(frame, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, dp(320)));

        TextView status = text("正在读取当前帧…", 12, 0xff2563eb);
        status.setGravity(Gravity.CENTER_HORIZONTAL);
        body.addView(status, matchWrapMargins(0, 10, 0, 0));

        AlertDialog dialog = new AlertDialog.Builder(this)
                .setTitle(provider.name + " · 来源预览")
                .setView(body)
                .setNegativeButton("关闭", null)
                .setNeutralButton("刷新帧", null)
                .create();
        Runnable refresh = () -> {
            if (!dialog.isShowing()) return;
            progress.setVisibility(View.VISIBLE);
            status.setText("正在读取当前帧…");
            status.setTextColor(0xff2563eb);
            dialog.getButton(AlertDialog.BUTTON_NEUTRAL).setEnabled(false);
            runAsync(() -> new PreviewResult("pattern".equals(provider.type)
                            ? buildPatternPreview() : loadBackendProviderPreview(provider.id)),
                    result -> {
                        if (!dialog.isShowing()) return;
                        Bitmap bitmap = ((PreviewResult) result).bitmap;
                        image.setImageBitmap(bitmap);
                        progress.setVisibility(View.GONE);
                        status.setText("当前帧 · " + bitmap.getWidth() + "×" + bitmap.getHeight());
                        status.setTextColor(0xff059669);
                        dialog.getButton(AlertDialog.BUTTON_NEUTRAL).setEnabled(true);
                    }, error -> {
                        if (!dialog.isShowing()) return;
                        progress.setVisibility(View.GONE);
                        status.setText(friendlyError(error));
                        status.setTextColor(0xffdc2626);
                        dialog.getButton(AlertDialog.BUTTON_NEUTRAL).setEnabled(true);
                    });
        };
        dialog.setOnShowListener(ignored -> {
            dialog.getButton(AlertDialog.BUTTON_NEUTRAL).setOnClickListener(view -> refresh.run());
            refresh.run();
        });
        dialog.show();
    }

    private Bitmap decodeBackendFrame(String encoded) throws IOException {
        byte[] frame;
        try {
            frame = android.util.Base64.decode(encoded, android.util.Base64.DEFAULT);
        } catch (IllegalArgumentException error) {
            throw new IOException("后端返回的预览帧无效", error);
        }
        if (frame.length < 24) throw new IOException("后端返回的预览帧不完整");
        byte[] magic = {'V', 'C', 'A', 'M', 'R', 'G', 'B', '1'};
        for (int index = 0; index < magic.length; ++index) {
            if (frame[index] != magic[index]) throw new IOException("后端返回的预览帧格式无效");
        }
        ByteBuffer header = ByteBuffer.wrap(frame).order(ByteOrder.LITTLE_ENDIAN);
        int width = header.getInt(8);
        int height = header.getInt(12);
        int payload = header.getInt(16);
        long expected = (long) width * height * 3;
        if (width < 2 || width > 1920 || height < 2 || height > 1920 ||
                payload != expected || frame.length != 24L + expected) {
            throw new IOException("后端返回的预览帧尺寸无效");
        }
        int[] pixels = new int[width * height];
        int offset = 24;
        for (int index = 0; index < pixels.length; ++index) {
            int red = frame[offset++] & 0xff;
            int green = frame[offset++] & 0xff;
            int blue = frame[offset++] & 0xff;
            pixels[index] = Color.rgb(red, green, blue);
        }
        return Bitmap.createBitmap(pixels, width, height, Bitmap.Config.ARGB_8888);
    }

    private Paint colorPaint(int color) { Paint paint = new Paint(); paint.setColor(color); return paint; }

    private void showTransformEditor(PendingProvider pending, Bitmap previewBitmap) {
        SourceTransformView editor = new SourceTransformView(this, previewBitmap);
        SourceTransformView.Setting[] settings = {
                SourceTransformView.Setting.decode(pending.view0),
                SourceTransformView.Setting.decode(pending.view1)
        };
        int[] active = {0};
        LinearLayout body = vertical();
        TextView cameraLabel = text("", 13, 0xff526171);
        LinearLayout targets = horizontal();
        Button camera0 = secondaryButton("目标相机 0");
        Button camera1 = secondaryButton("目标相机 1");
        targets.addView(camera0, weighted()); targets.addView(camera1, weighted());
        body.addView(targets, matchWrap()); body.addView(cameraLabel, matchWrap());
        body.addView(editor, matchWrapMargins(0, 8, 0, 0));
        TextView scaleLabel = text("缩放 1.00×", 13, 0xff526171);
        SeekBar zoom = new SeekBar(this); zoom.setMax(790);
        Button rotate = secondaryButton("顺时针旋转 90°");
        body.addView(scaleLabel, matchWrapMargins(0, 8, 0, 0));
        body.addView(zoom, matchWrap()); body.addView(rotate, matchWrap());
        body.addView(text("白色取景框保持不变；单指拖动源画面，双指或滑杆无级缩放。配置分别保存到两个目标相机。",
                12, 0xff667383), matchWrapMargins(0, 6, 0, 0));

        Runnable refresh = () -> {
            TargetCameraSpec spec = targetCameras[active[0]];
            editor.setTargetAspect(spec.width, spec.height);
            editor.setSetting(settings[active[0]]);
            zoom.setProgress(Math.round((settings[active[0]].scale - .1f) * 100f));
            cameraLabel.setText(spec.description());
            scaleLabel.setText(String.format(Locale.US, "缩放 %.2f× · 旋转 %d°",
                    settings[active[0]].scale, settings[active[0]].rotation));
        };
        View.OnClickListener switcher = view -> {
            settings[active[0]] = editor.getSetting();
            active[0] = view == camera0 ? 0 : 1;
            refresh.run();
        };
        camera0.setOnClickListener(switcher); camera1.setOnClickListener(switcher);
        zoom.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar bar, int progress, boolean fromUser) {
                float value = .1f + progress / 100f;
                editor.setScale(value); settings[active[0]].scale = value;
                scaleLabel.setText(String.format(Locale.US, "缩放 %.2f× · 旋转 %d°",
                        value, editor.getSetting().rotation));
            }
            @Override public void onStartTrackingTouch(SeekBar bar) { }
            @Override public void onStopTrackingTouch(SeekBar bar) { }
        });
        rotate.setOnClickListener(view -> {
            editor.rotateClockwise(); settings[active[0]] = editor.getSetting(); refresh.run();
        });
        refresh.run();
        ScrollView scroll = new ScrollView(this); scroll.addView(body);
        new AlertDialog.Builder(this).setTitle(pending.editing ? "更新双相机取景" : "设置双相机取景")
                .setView(scroll).setNegativeButton("取消", null)
                .setPositiveButton(pending.editing ? "保存修改" : "添加源", (dialog, which) -> {
                    settings[active[0]] = editor.getSetting();
                    pending.view0 = settings[0].encode(); pending.view1 = settings[1].encode();
                    if (pending.editing) updateProvider(pending);
                    else if ("image".equals(pending.type)) importImage(pending);
                    else if ("video".equals(pending.type)) importVideo(pending);
                    else addRemoteProvider(pending);
                }).show();
    }

    private void updateProvider(PendingProvider pending) {
        toast("正在保存并验证修改…");
        runAsync(() -> {
            BackendClient.Result updated = BackendClient.controller("provider-update",
                    pending.id, BackendClient.encode(pending.name), BackendClient.encode(pending.source),
                    Integer.toString(pending.fps), Integer.toString(pending.maxWidth),
                    Integer.toString(pending.maxHeight), pending.view0, pending.view1);
            updated.requireSuccess();
            return updated;
        }, result -> {
            toast("视频源已更新");
            refreshProviders(true);
        });
    }

    private void importImage(PendingProvider pending) {
        toast("正在转换并导入图片…");
        runAsync(() -> {
            byte[] frame = buildFrame(pending.uri, pending.maxWidth, pending.maxHeight);
            BackendClient.Result added = BackendClient.controller("provider-add", pending.id, "image",
                    BackendClient.encode(pending.name), "");
            added.requireSuccess();
            try {
                configureProvider(pending);
                BackendClient.Result published = BackendClient.controller(
                        frame, "provider-publish-stdin", pending.id);
                published.requireSuccess();
                return published;
            } catch (Exception error) {
                BackendClient.controller("provider-remove", pending.id);
                throw error;
            }
        }, result -> { toast("图片提供器已添加"); refreshProviders(); });
    }

    private void importVideo(PendingProvider pending) {
        toast("正在导入本地视频…");
        runAsync(() -> {
            String modulePath = "/data/vendor/camera/vcam/providers/" + pending.id + "/source.media";
            BackendClient.Result added = BackendClient.controller("provider-add", pending.id, "video",
                    BackendClient.encode(pending.name), BackendClient.encode(modulePath));
            added.requireSuccess();
            File temporary = File.createTempFile("vcam-import-", ".media", getCacheDir());
            try {
                configureProvider(pending);
                try (InputStream input = getContentResolver().openInputStream(pending.uri);
                     FileOutputStream output = new FileOutputStream(temporary)) {
                    if (input == null) throw new IOException("无法读取所选视频");
                    byte[] buffer = new byte[64 * 1024];
                    int count;
                    while ((count = input.read(buffer)) >= 0) output.write(buffer, 0, count);
                }
                try (FileInputStream input = new FileInputStream(temporary)) {
                    BackendClient.Result imported = BackendClient.controller(
                            input, temporary.length(), "provider-import-media", pending.id);
                    imported.requireSuccess();
                }
                BackendClient.Result started = BackendClient.controller("provider-start", pending.id);
                started.requireSuccess();
                return started;
            } catch (Exception error) {
                BackendClient.controller("provider-remove", pending.id);
                throw error;
            } finally {
                if (!temporary.delete()) temporary.deleteOnExit();
            }
        }, result -> { toast("本地视频提供器已添加"); refreshProviders(); });
    }

    private void addRemoteProvider(PendingProvider pending) {
        runAsync(() -> {
            BackendClient.Result added = BackendClient.controller("provider-add", pending.id, pending.type,
                    BackendClient.encode(pending.name), BackendClient.encode(pending.source));
            added.requireSuccess();
            try {
                configureProvider(pending);
                BackendClient.Result started = BackendClient.controller("provider-start", pending.id);
                started.requireSuccess(); return started;
            } catch (Exception error) {
                BackendClient.controller("provider-remove", pending.id); throw error;
            }
        }, result -> { toast("提供器已添加"); refreshProviders(); });
    }

    private void configureProvider(PendingProvider pending) throws IOException {
        BackendClient.Result configured = BackendClient.controller("provider-config-set",
                pending.id, Integer.toString(pending.fps), Integer.toString(pending.maxWidth),
                Integer.toString(pending.maxHeight), pending.view0, pending.view1);
        configured.requireSuccess();
    }

    private byte[] buildFrame(Uri uri, int maxWidth, int maxHeight) throws IOException {
        BitmapFactory.Options bounds = new BitmapFactory.Options();
        bounds.inJustDecodeBounds = true;
        try (InputStream input = getContentResolver().openInputStream(uri)) {
            BitmapFactory.decodeStream(input, null, bounds);
        }
        int sample = 1;
        while (bounds.outWidth / sample > maxWidth * 2 || bounds.outHeight / sample > maxHeight * 2) sample *= 2;
        BitmapFactory.Options options = new BitmapFactory.Options();
        options.inSampleSize = sample;
        Bitmap source;
        try (InputStream input = getContentResolver().openInputStream(uri)) {
            source = BitmapFactory.decodeStream(input, null, options);
        }
        if (source == null) throw new IOException("无法解码所选图片");
        float scale = Math.min(1f, Math.min((float) maxWidth / source.getWidth(),
                (float) maxHeight / source.getHeight()));
        int frameWidth = Math.max(2, Math.round(source.getWidth() * scale));
        int frameHeight = Math.max(2, Math.round(source.getHeight() * scale));
        Bitmap target = scale < 1f ? Bitmap.createScaledBitmap(source, frameWidth, frameHeight, true) : source;
        if (target != source) source.recycle();
        int payload = frameWidth * frameHeight * 3;
        ByteBuffer frame = ByteBuffer.allocate(24 + payload).order(ByteOrder.LITTLE_ENDIAN);
        frame.put(new byte[]{'V', 'C', 'A', 'M', 'R', 'G', 'B', '1'});
        frame.putInt(frameWidth); frame.putInt(frameHeight);
        frame.putInt(payload); frame.putInt(++frameSequence);
        int[] pixels = new int[frameWidth * frameHeight];
        target.getPixels(pixels, 0, frameWidth, 0, 0, frameWidth, frameHeight);
        target.recycle();
        for (int pixel : pixels) {
            frame.put((byte) Color.red(pixel));
            frame.put((byte) Color.green(pixel));
            frame.put((byte) Color.blue(pixel));
        }
        return frame.array();
    }

    private void runProviderAction(String command, String id) {
        runAsync(() -> BackendClient.controller(command, id), result -> {
            result.requireSuccess();
            if ("provider-remove".equals(command)) lastRouteRefresh = 0;
            refreshProviders();
        });
    }

    private void loadTargetCameraSpecs() {
        try {
            CameraManager manager = getSystemService(CameraManager.class);
            for (int index = 0; index < 2; ++index) {
                String id = Integer.toString(index);
                CameraCharacteristics characteristics = manager.getCameraCharacteristics(id);
                Rect active = characteristics.get(CameraCharacteristics.SENSOR_INFO_ACTIVE_ARRAY_SIZE);
                Integer orientation = characteristics.get(CameraCharacteristics.SENSOR_ORIENTATION);
                int sensorWidth = active == null ? 4000 : active.width();
                int sensorHeight = active == null ? 3000 : active.height();
                int angle = orientation == null ? (index == 0 ? 90 : 270) : orientation;
                int displayWidth = angle == 90 || angle == 270 ? sensorHeight : sensorWidth;
                int displayHeight = angle == 90 || angle == 270 ? sensorWidth : sensorHeight;
                targetCameras[index] = new TargetCameraSpec(id, displayWidth, displayHeight, angle);
            }
        } catch (Exception ignored) { }
    }

    private EditText numericInput(String hint, String value) {
        EditText input = input(hint); input.setText(value);
        input.setInputType(android.text.InputType.TYPE_CLASS_NUMBER); return input;
    }

    private int parseBounded(EditText input, int minimum, int maximum, int fallback) {
        try { return Math.max(minimum, Math.min(maximum,
                Integer.parseInt(input.getText().toString().trim()))); }
        catch (NumberFormatException ignored) { return fallback; }
    }

    private void confirmRemove(Provider provider) {
        new AlertDialog.Builder(this).setTitle("删除提供器")
                .setMessage("删除“" + provider.name + "”及引用它的路由？")
                .setNegativeButton("取消", null)
                .setPositiveButton("删除", (dialog, which) ->
                        runProviderAction("provider-remove", provider.id)).show();
    }

    private void refreshRoutes(boolean force) {
        if (!routes.isEmpty()) renderRoutes();
        long age = SystemClock.elapsedRealtime() - lastRouteRefresh;
        if (!force && lastRouteRefresh > 0 && age < 5_000) return;
        if (routeRefreshInFlight) {
            routeRefreshPending |= force;
            return;
        }
        routeRefreshInFlight = true;
        routeRefreshText.setText(routes.isEmpty() ? "正在读取路由…" : "正在后台更新…");
        if (routes.isEmpty()) {
            routeList.removeAllViews();
            routeList.addView(text("正在读取已配置路由…", 14, 0xff667383));
        }
        runAsync(() -> {
            BackendClient.Result routeResult = BackendClient.controller("routes");
            routeResult.requireSuccess();
            List<Provider> latestProviders = null;
            if (providers.isEmpty() || SystemClock.elapsedRealtime() - lastProviderRefresh > 5_000) {
                BackendClient.Result providerResult = BackendClient.controller("providers");
                providerResult.requireSuccess();
                latestProviders = parseProviders(providerResult.output);
            }
            return new RoutesResult(parseRoutes(routeResult.output), latestProviders);
        }, result -> {
            RoutesResult latest = (RoutesResult) result;
            routes.clear(); routes.putAll(latest.routes);
            if (latest.providers != null) {
                providers.clear(); providers.addAll(latest.providers);
                lastProviderRefresh = SystemClock.elapsedRealtime();
            }
            lastRouteRefresh = SystemClock.elapsedRealtime();
            routeRefreshInFlight = false;
            routeRefreshText.setText("刚刚更新 · " + routePackages().size() + " 个应用路由");
            renderRoutes();
            if (routeRefreshPending) {
                routeRefreshPending = false;
                refreshRoutes(true);
            }
        }, error -> {
            routeRefreshInFlight = false;
            routeRefreshText.setText("更新失败，保留现有路由");
            showError(error);
        });
    }

    private void renderRoutes() {
        if (routeList == null) return;
        routeList.removeAllViews();
        Set<String> packages = routePackages();
        if (packages.isEmpty()) {
            LinearLayout empty = card();
            empty.addView(cardHeading("尚未添加应用路由", "点击右上角“新增”，选择应用并配置相机来源"));
            routeList.addView(empty, matchWrap());
            return;
        }
        for (String packageName : packages) {
            AppEntry app = appEntryForPackage(packageName);
            LinearLayout routeCard = card();
            LinearLayout top = horizontal();
            top.setGravity(Gravity.CENTER_VERTICAL);
            ImageView icon = new ImageView(this);
            if (app.installed) {
                Drawable drawable = appIcons.get(packageName);
                if (drawable == null) {
                    try { drawable = getPackageManager().getApplicationIcon(packageName); }
                    catch (PackageManager.NameNotFoundException ignored) { }
                    if (drawable != null) appIcons.put(packageName, drawable);
                }
                icon.setImageDrawable(drawable);
            }
            icon.setBackground(roundRect(app.installed ? 0xffeff6ff : 0xfff1f5f9, 14));
            top.addView(icon, new LinearLayout.LayoutParams(dp(46), dp(46)));
            LinearLayout identity = vertical();
            TextView name = text(app.installed ? app.label : packageName, 15, 0xff111827);
            name.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
            identity.addView(name);
            if (app.installed) identity.addView(text(packageName, 11, 0xff64748b));
            top.addView(identity, weightedMargins(12, 0, 8, 0));
            top.addView(pill(app.installed ? "可用" : "暂不可用",
                    app.installed ? 0xffecfdf5 : 0xfffff7ed,
                    app.installed ? 0xff059669 : 0xffc2410c));
            routeCard.addView(top);
            LinearLayout mapping = vertical();
            mapping.setPadding(dp(12), dp(9), dp(12), dp(9));
            mapping.setBackground(roundRect(0xfff8fafc, 10));
            mapping.addView(text("相机 0  →  " + routeProviderName("0", routes.get(packageName + "\t0")),
                    12, 0xff475569));
            mapping.addView(text("相机 1  →  " + routeProviderName("1", routes.get(packageName + "\t1")),
                    12, 0xff475569), matchWrapMargins(0, 5, 0, 0));
            routeCard.addView(mapping, matchWrapMargins(0, 12, 0, 0));
            LinearLayout actions = horizontal();
            Button edit = secondaryButton("修改");
            edit.setOnClickListener(view -> showRouteEditor(app));
            Button remove = dangerButton("删除");
            remove.setOnClickListener(view -> confirmRemoveRoute(app));
            actions.addView(edit, weighted());
            actions.addView(remove, weightedMargins(8, 0, 0, 0));
            routeCard.addView(actions, matchWrapMargins(0, 12, 0, 0));
            routeList.addView(routeCard, matchWrapMargins(0, 0, 0, 12));
        }
    }

    private void showAddRouteDialog() {
        LinearLayout loadingBody = horizontal();
        loadingBody.setGravity(Gravity.CENTER_VERTICAL);
        loadingBody.setPadding(dp(24), dp(16), dp(24), dp(16));
        loadingBody.addView(new ProgressBar(this), new LinearLayout.LayoutParams(dp(30), dp(30)));
        loadingBody.addView(text("正在读取未配置的应用…", 13, 0xff475569),
                weightedMargins(12, 0, 0, 0));
        AlertDialog loading = new AlertDialog.Builder(this).setTitle("新增应用路由")
                .setView(loadingBody).setNegativeButton("取消", null).create();
        loading.show();
        final boolean scanApps = appsDirty || allApps.isEmpty() ||
                SystemClock.elapsedRealtime() - lastAppRefresh > 30_000;
        final List<AppEntry> cachedApps = new ArrayList<>(allApps);
        runAsync(() -> {
            BackendClient.Result routeResult = BackendClient.controller("routes");
            routeResult.requireSuccess();
            BackendClient.Result providerResult = BackendClient.controller("providers");
            providerResult.requireSuccess();
            List<AppEntry> apps = scanApps ? scanInstalledApps() : cachedApps;
            return new RouteSetupResult(parseRoutes(routeResult.output),
                    parseProviders(providerResult.output), apps);
        }, result -> {
            if (!loading.isShowing()) return;
            loading.dismiss();
            RouteSetupResult setup = (RouteSetupResult) result;
            routes.clear(); routes.putAll(setup.routes);
            providers.clear(); providers.addAll(setup.providers);
            allApps.clear(); allApps.addAll(setup.apps);
            appsDirty = false;
            lastAppRefresh = lastProviderRefresh = lastRouteRefresh = SystemClock.elapsedRealtime();
            renderRoutes();
            showAppPicker();
        }, error -> {
            if (loading.isShowing()) loading.dismiss();
            showError(error);
        });
    }

    private void showAppPicker() {
        Set<String> configured = routePackages();
        List<AppEntry> candidates = new ArrayList<>();
        for (AppEntry app : allApps) {
            if (!configured.contains(app.packageName)) candidates.add(app);
        }
        if (candidates.isEmpty()) {
            new AlertDialog.Builder(this).setTitle("没有可添加的应用")
                    .setMessage("所有可见应用都已经配置了路由。")
                    .setPositiveButton("知道了", null).show();
            return;
        }
        LinearLayout body = dialogForm();
        EditText search = input("搜索应用或包名");
        body.addView(search);
        ListView list = new ListView(this);
        list.setDivider(new ColorDrawable(Color.TRANSPARENT));
        list.setDividerHeight(dp(7));
        list.setSelector(android.R.color.transparent);
        List<AppEntry> visible = new ArrayList<>(candidates);
        AppPickerAdapter adapter = new AppPickerAdapter(visible);
        list.setAdapter(adapter);
        body.addView(list, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(460)));
        AlertDialog picker = new AlertDialog.Builder(this).setTitle("选择未配置的应用")
                .setView(body).setNegativeButton("取消", null).create();
        search.addTextChangedListener(new TextWatcher() {
            @Override public void beforeTextChanged(CharSequence s, int start, int count, int after) { }
            @Override public void onTextChanged(CharSequence s, int start, int before, int count) {
                String needle = s.toString().trim().toLowerCase(Locale.ROOT);
                visible.clear();
                for (AppEntry app : candidates) {
                    if (needle.isEmpty() || app.label.toLowerCase(Locale.ROOT).contains(needle) ||
                            app.packageName.toLowerCase(Locale.ROOT).contains(needle)) visible.add(app);
                }
                adapter.notifyDataSetChanged();
            }
            @Override public void afterTextChanged(Editable s) { }
        });
        list.setOnItemClickListener((parent, view, position, id) -> {
            AppEntry selected = visible.get(position);
            picker.dismiss();
            showRouteEditor(selected);
        });
        picker.show();
    }

    private void showRouteEditor(AppEntry app) {
        List<String> ids = new ArrayList<>();
        List<String> labels = new ArrayList<>();
        ids.add(""); labels.add("默认：使用目标物理相机");
        for (Provider provider : providers) {
            ids.add(provider.id);
            labels.add(provider.name + (provider.running ? "" : "（已停止）"));
        }
        LinearLayout form = dialogForm();
        Spinner camera0 = routeSpinner(labels, ids, routes.get(app.packageName + "\t0"));
        Spinner camera1 = routeSpinner(labels, ids, routes.get(app.packageName + "\t1"));
        form.addView(text(app.installed ? app.label + "\n" + app.packageName
                : app.packageName + "\n暂不可用（路由仍会保留）", 14, 0xff526171));
        form.addView(label("目标相机 0")); form.addView(camera0);
        form.addView(label("目标相机 1")); form.addView(camera1);
        TextView error = text("请至少为一个目标相机选择替换来源", 12, 0xffdc2626);
        error.setVisibility(View.GONE);
        form.addView(error, matchWrapMargins(0, 8, 0, 0));
        AlertDialog dialog = new AlertDialog.Builder(this).setTitle("应用路由配置")
                .setView(form).setNegativeButton("取消", null)
                .setPositiveButton("保存", null).create();
        dialog.setOnShowListener(ignored ->
                dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(view -> {
                    String provider0 = ids.get(camera0.getSelectedItemPosition());
                    String provider1 = ids.get(camera1.getSelectedItemPosition());
                    if (provider0.isEmpty() && provider1.isEmpty()) {
                        error.setVisibility(View.VISIBLE);
                        return;
                    }
                    dialog.dismiss();
                    saveAppRoutes(app.packageName, provider0, provider1);
                }));
        dialog.show();
    }

    private Spinner routeSpinner(List<String> labels, List<String> ids, String selected) {
        Spinner spinner = new Spinner(this);
        spinner.setAdapter(new ArrayAdapter<>(this,
                android.R.layout.simple_spinner_dropdown_item, labels));
        int index = selected == null ? 0 : ids.indexOf(selected);
        spinner.setSelection(Math.max(0, index));
        return spinner;
    }

    private void saveAppRoutes(String packageName, String provider0, String provider1) {
        runAsync(() -> BackendClient.controller("route-save", packageName, provider0, provider1), result -> {
            result.requireSuccess();
            routes.remove(packageName + "\t0");
            routes.remove(packageName + "\t1");
            if (!provider0.isEmpty()) routes.put(packageName + "\t0", provider0);
            if (!provider1.isEmpty()) routes.put(packageName + "\t1", provider1);
            lastRouteRefresh = SystemClock.elapsedRealtime();
            renderRoutes();
            toast("应用路由已保存，新相机会话开始时生效");
        });
    }

    private void confirmRemoveRoute(AppEntry app) {
        new AlertDialog.Builder(this).setTitle("删除应用路由")
                .setMessage("删除 “" + (app.installed ? app.label : app.packageName) + "” 的全部相机路由？")
                .setNegativeButton("取消", null)
                .setPositiveButton("删除", (dialog, which) ->
                        runAsync(() -> BackendClient.controller("route-save", app.packageName, "", ""), result -> {
                            result.requireSuccess();
                            routes.remove(app.packageName + "\t0");
                            routes.remove(app.packageName + "\t1");
                            lastRouteRefresh = SystemClock.elapsedRealtime();
                            renderRoutes();
                            toast("应用路由已删除");
                        })).show();
    }

    private Set<String> routePackages() {
        Set<String> packages = new TreeSet<>();
        for (String key : routes.keySet()) {
            int split = key.lastIndexOf('\t');
            if (split > 0) packages.add(key.substring(0, split));
        }
        return packages;
    }

    private String routeProviderName(String target, String providerId) {
        if (providerId == null || providerId.isEmpty()) return "目标物理相机 " + target + "（默认）";
        for (Provider provider : providers) {
            if (provider.id.equals(providerId)) {
                return provider.name + (provider.running ? "" : "（已停止）");
            }
        }
        return providerId + "（来源不可用）";
    }

    private AppEntry appEntryForPackage(String packageName) {
        for (AppEntry app : allApps) {
            if (packageName.equals(app.packageName)) return app;
        }
        AppEntry installed = installedApp(packageName);
        if (installed != null) {
            allApps.add(installed);
            return installed;
        }
        return new AppEntry(packageName, packageName, false);
    }

    private AppEntry installedApp(String packageName) {
        PackageManager pm = getPackageManager();
        try {
            ApplicationInfo info = pm.getApplicationInfo(packageName, 0);
            CharSequence label = pm.getApplicationLabel(info);
            String labelText = label == null || label.length() == 0 ? packageName : label.toString();
            return new AppEntry(labelText, packageName, true);
        } catch (PackageManager.NameNotFoundException ignored) {
            return null;
        }
    }

    private List<AppEntry> scanInstalledApps() {
        PackageManager pm = getPackageManager();
        List<AppEntry> scanned = new ArrayList<>();
        for (ApplicationInfo info : pm.getInstalledApplications(0)) {
            CharSequence label = pm.getApplicationLabel(info);
            String labelText = label == null || label.length() == 0
                    ? info.packageName : label.toString();
            scanned.add(new AppEntry(labelText, info.packageName, true));
        }
        scanned.sort(Comparator.comparing(entry -> entry.label.toLowerCase(Locale.ROOT)));
        return scanned;
    }

    private List<Provider> parseProviders(String output) {
        List<Provider> values = new ArrayList<>();
        for (String line : output.split("\\r?\\n")) {
            String[] fields = line.split("\\t", -1);
            if (fields.length < 7 || !"PROVIDER".equals(fields[0])) continue;
            int fps = fields.length >= 12 ? parseInt(fields[7], 15) : 15;
            int maxWidth = fields.length >= 12 ? parseInt(fields[8], 1280) : 1280;
            int maxHeight = fields.length >= 12 ? parseInt(fields[9], 720) : 720;
            String view0 = fields.length >= 12 ? fields[10] : "0,1000,500,500";
            String view1 = fields.length >= 12 ? fields[11] : "0,1000,500,500";
            values.add(new Provider(fields[1], fields[2], BackendClient.decode(fields[3]),
                    BackendClient.decode(fields[4]), "true".equals(fields[5]),
                    "true".equals(fields[6]), fps, maxWidth, maxHeight, view0, view1));
        }
        return values;
    }

    private Map<String, String> parseRoutes(String output) {
        Map<String, String> values = new HashMap<>();
        for (String line : output.split("\\r?\\n")) {
            String[] fields = line.split("\\t", -1);
            if (fields.length == 4 && "ROUTE".equals(fields[0])) {
                values.put(fields[1] + "\t" + fields[2], fields[3]);
            }
        }
        return values;
    }

    private Map<String, String> parseProperties(String output) {
        Map<String, String> values = new HashMap<>();
        for (String line : output.split("\\r?\\n")) {
            int split = line.indexOf('=');
            if (split > 0) values.put(line.substring(0, split), line.substring(split + 1));
        }
        return values;
    }

    private interface Work { BackendClient.Result run() throws Exception; }
    private interface Success { void accept(BackendClient.Result result) throws Exception; }
    private interface Failure { void accept(Throwable error); }

    private void runAsync(Work work, Success success) {
        runAsync(work, success, this::showError);
    }

    private void runAsync(Work work, Success success, Failure failure) {
        worker.execute(() -> {
            try {
                BackendClient.Result result = work.run();
                main.post(() -> {
                    try { success.accept(result); }
                    catch (Exception error) { failure.accept(error); }
                });
            } catch (Exception error) { main.post(() -> failure.accept(error)); }
        });
    }

    private void showError(Throwable error) {
        toast(friendlyError(error));
    }

    private String friendlyError(Throwable error) {
        String message = error.getMessage();
        return message == null || message.isEmpty() ? error.toString() : message;
    }

    private boolean isNetworkType(String type) {
        return "http".equals(type) || "https".equals(type) ||
                "hls".equals(type) || "rtsp".equals(type);
    }

    private String protocolPrefix(String type) {
        if ("https".equals(type)) return "https://";
        if ("rtsp".equals(type)) return "rtsp://";
        return "http://";
    }

    private int typeIndex(String type) {
        for (int index = 0; index < TYPE_CODES.length; ++index) {
            if (TYPE_CODES[index].equals(type)) return index;
        }
        return 0;
    }

    private int parseInt(String value, int fallback) {
        try { return Integer.parseInt(value); }
        catch (NumberFormatException ignored) { return fallback; }
    }

    private String displayName(Uri uri) {
        try (android.database.Cursor cursor = getContentResolver().query(
                uri, new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                String value = cursor.getString(0);
                if (value != null && !value.isEmpty()) return value;
            }
        } catch (Exception ignored) { }
        return uri.getLastPathSegment() == null ? "已选择文件" : uri.getLastPathSegment();
    }

    private void showPage(View page) {
        statusPage.setVisibility(page == statusPage ? View.VISIBLE : View.GONE);
        sourcesPage.setVisibility(page == sourcesPage ? View.VISIBLE : View.GONE);
        appsPage.setVisibility(page == appsPage ? View.VISIBLE : View.GONE);
        int selected = page == statusPage ? 0 : page == sourcesPage ? 1 : 2;
        for (int index = 0; index < navigationItems.size(); ++index) {
            TextView item = navigationItems.get(index);
            boolean active = index == selected;
            item.setTextColor(active ? 0xff2563eb : 0xff64748b);
            item.setBackground(roundRect(active ? 0xffeff6ff : Color.TRANSPARENT, 14));
            item.setTypeface(Typeface.DEFAULT, active ? Typeface.BOLD : Typeface.NORMAL);
        }
    }

    private String yesNo(String value) { return "true".equals(value) ? "正常" : "异常"; }
    private String friendlyCount(String value) {
        return value == null || value.isEmpty() || "unavailable".equals(value) ? "系统管理" : value;
    }
    private String typeName(String type) {
        for (int i = 0; i < TYPE_CODES.length; ++i) if (TYPE_CODES[i].equals(type)) return TYPE_LABELS[i];
        return "physical".equals(type) ? "物理相机" : type;
    }
    private String providerSymbol(String type) {
        if ("physical".equals(type)) return "CAM";
        if ("image".equals(type)) return "IMG";
        if ("pattern".equals(type)) return "RGB";
        return "NET";
    }
    private int providerAccent(String type) {
        if ("physical".equals(type)) return 0xff2563eb;
        if ("image".equals(type)) return 0xff7c3aed;
        if ("pattern".equals(type)) return 0xffea580c;
        return 0xff0891b2;
    }
    private int providerTint(String type) {
        if ("physical".equals(type)) return 0xffeff6ff;
        if ("image".equals(type)) return 0xfff5f3ff;
        if ("pattern".equals(type)) return 0xfffff7ed;
        return 0xffecfeff;
    }
    private void toast(String message) { Toast.makeText(this, message, Toast.LENGTH_LONG).show(); }
    private int dp(int value) { return Math.round(value * getResources().getDisplayMetrics().density); }
    private LinearLayout vertical() { LinearLayout view = new LinearLayout(this); view.setOrientation(LinearLayout.VERTICAL); return view; }
    private LinearLayout horizontal() { LinearLayout view = new LinearLayout(this); view.setOrientation(LinearLayout.HORIZONTAL); return view; }
    private LinearLayout pageBody() { LinearLayout view = vertical(); view.setPadding(dp(18), dp(22), dp(18), dp(24)); return view; }
    private LinearLayout dialogForm() { LinearLayout view = vertical(); view.setPadding(dp(22), dp(8), dp(22), dp(4)); return view; }
    private TextView text(String value, int size, int color) { TextView view = new TextView(this); view.setText(value); view.setTextSize(size); view.setTextColor(color); view.setLineSpacing(0, 1.15f); return view; }
    private TextView label(String value) { TextView view = text(value, 12, 0xff64748b); view.setTypeface(Typeface.DEFAULT, Typeface.BOLD); view.setPadding(0, dp(14), 0, dp(5)); return view; }
    private LinearLayout pageTitle(String title, String subtitle) {
        LinearLayout block = vertical();
        TextView heading = text(title, 25, 0xff0f172a);
        heading.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        block.addView(heading);
        block.addView(text(subtitle, 12, 0xff64748b), matchWrapMargins(0, 3, 0, 0));
        return block;
    }
    private LinearLayout cardHeading(String title, String subtitle) {
        LinearLayout block = vertical();
        TextView heading = text(title, 15, 0xff111827);
        heading.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        block.addView(heading);
        block.addView(text(subtitle, 11, 0xff64748b), matchWrapMargins(0, 3, 0, 0));
        return block;
    }
    private LinearLayout card() {
        LinearLayout view = vertical();
        view.setPadding(dp(16), dp(15), dp(16), dp(15));
        view.setBackground(roundRect(Color.WHITE, 18));
        view.setElevation(dp(1));
        return view;
    }
    private TextView metricCard(String label, String value, int tint, int accent) {
        LinearLayout metric = card();
        TextView number = text(value, 28, accent);
        number.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        TextView caption = text(label, 12, 0xff64748b);
        metric.addView(number);
        metric.addView(caption, matchWrapMargins(0, 2, 0, 0));
        metric.setBackground(roundRect(tint, 18));
        number.setTag(metric);
        return number;
    }
    private TextView pill(String value, int background, int foreground) {
        TextView view = text(value, 11, foreground);
        view.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        view.setGravity(Gravity.CENTER);
        view.setPadding(dp(10), dp(5), dp(10), dp(5));
        view.setBackground(roundRect(background, 40));
        return view;
    }
    private TextView navigationItem(String label, String symbol) {
        TextView item = text(symbol + "  " + label, 12, 0xff64748b);
        item.setGravity(Gravity.CENTER);
        item.setPadding(dp(8), dp(11), dp(8), dp(11));
        navigationItems.add(item);
        return item;
    }
    private EditText input(String hint) {
        EditText view = new EditText(this);
        view.setHint(hint); view.setSingleLine(true); view.setTextSize(14);
        view.setPadding(dp(14), 0, dp(14), 0);
        view.setBackground(roundRect(0xfff1f5f9, 12));
        view.setMinHeight(dp(50));
        return view;
    }
    private Button primaryButton(String value) { return styledButton(value, Color.WHITE, 0xff2563eb); }
    private TextView compactPrimaryButton(String value) {
        TextView view = text(value, 13, Color.WHITE);
        view.setTypeface(Typeface.DEFAULT, Typeface.BOLD); view.setGravity(Gravity.CENTER);
        view.setPadding(dp(16), dp(10), dp(16), dp(10));
        view.setBackground(ripple(0xff2563eb, 14)); view.setClickable(true);
        return view;
    }
    private TextView compactSecondaryButton(String value) {
        TextView view = text(value, 12, 0xff334155);
        view.setTypeface(Typeface.DEFAULT, Typeface.BOLD); view.setGravity(Gravity.CENTER);
        view.setPadding(dp(13), dp(8), dp(13), dp(8));
        view.setBackground(ripple(0xffeef2f7, 12)); view.setClickable(true);
        return view;
    }
    private Button secondaryButton(String value) { return styledButton(value, 0xff334155, 0xfff1f5f9); }
    private Button dangerButton(String value) { return styledButton(value, 0xffdc2626, 0xfffef2f2); }
    private Button styledButton(String value, int foreground, int background) {
        Button button = new Button(this);
        button.setText(value); button.setTextColor(foreground); button.setTextSize(13);
        button.setAllCaps(false); button.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        button.setGravity(Gravity.CENTER); button.setMinHeight(0); button.setMinimumHeight(0);
        button.setPadding(dp(14), dp(10), dp(14), dp(10));
        button.setBackground(ripple(background, 13)); button.setStateListAnimator(null);
        return button;
    }
    private Drawable roundRect(int color, int radius) {
        GradientDrawable shape = new GradientDrawable();
        shape.setColor(color); shape.setCornerRadius(dp(radius));
        return shape;
    }
    private Drawable roundGradient(int start, int end, int radius) {
        GradientDrawable shape = new GradientDrawable(
                GradientDrawable.Orientation.TL_BR, new int[]{start, end});
        shape.setCornerRadius(dp(radius));
        return shape;
    }
    private Drawable ripple(int color, int radius) {
        return new RippleDrawable(ColorStateList.valueOf(0x22000000),
                roundRect(color, radius), null);
    }
    private LinearLayout.LayoutParams matchWrap() { return new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT); }
    private FrameLayout.LayoutParams matchMatch() { return new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT); }
    private LinearLayout.LayoutParams weighted() { return new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1); }
    private LinearLayout.LayoutParams weightedMargins(int l, int t, int r, int b) { LinearLayout.LayoutParams p = weighted(); p.setMargins(dp(l), dp(t), dp(r), dp(b)); return p; }
    private LinearLayout.LayoutParams matchWrapMargins(int l, int t, int r, int b) { LinearLayout.LayoutParams p = matchWrap(); p.setMargins(dp(l), dp(t), dp(r), dp(b)); return p; }

    @Override protected void onDestroy() {
        try { unregisterReceiver(packageReceiver); } catch (IllegalArgumentException ignored) { }
        worker.shutdownNow();
        super.onDestroy();
    }

    private static final class Provider {
        final String id, type, name, source, view0, view1;
        final boolean removable, running;
        final int fps, maxWidth, maxHeight;
        Provider(String id, String type, String name, String source, boolean removable, boolean running,
                 int fps, int maxWidth, int maxHeight, String view0, String view1) {
            this.id = id; this.type = type; this.name = name; this.source = source;
            this.removable = removable; this.running = running;
            this.fps = fps; this.maxWidth = maxWidth; this.maxHeight = maxHeight;
            this.view0 = view0; this.view1 = view1;
        }
    }
    private static final class PendingProvider {
        final String id, type, name, source;
        final int fps, maxWidth, maxHeight;
        Uri uri;
        boolean editing;
        String view0 = "0,1000,500,500";
        String view1 = "0,1000,500,500";
        PendingProvider(String id, String type, String name, String source,
                        int fps, int maxWidth, int maxHeight) {
            this.id = id; this.type = type; this.name = name; this.source = source;
            this.fps = fps; this.maxWidth = maxWidth; this.maxHeight = maxHeight;
        }
    }
    private static final class AddProviderSession {
        final Provider existing;
        final EditText name, source, fps, maxWidth, maxHeight;
        final Spinner type;
        final TextView sourceLabel, selectedFile, progressText, errorText;
        final LinearLayout sourceBlock, fileBlock, progressRow;
        final Button chooseFile;
        final Map<String, String> urls = new HashMap<>();
        final Map<String, Uri> files = new HashMap<>();
        AlertDialog dialog;
        String lastType;
        boolean busy;
        AddProviderSession(Provider existing, EditText name, Spinner type, EditText source,
                           TextView sourceLabel, LinearLayout sourceBlock, LinearLayout fileBlock,
                           Button chooseFile, TextView selectedFile, EditText fps, EditText maxWidth,
                           EditText maxHeight, LinearLayout progressRow, TextView progressText,
                           TextView errorText) {
            this.existing = existing; this.name = name; this.type = type; this.source = source;
            this.sourceLabel = sourceLabel; this.sourceBlock = sourceBlock; this.fileBlock = fileBlock;
            this.chooseFile = chooseFile; this.selectedFile = selectedFile; this.fps = fps;
            this.maxWidth = maxWidth; this.maxHeight = maxHeight; this.progressRow = progressRow;
            this.progressText = progressText; this.errorText = errorText;
        }
    }
    private static final class TargetCameraSpec {
        final String id; final int width, height, sensorOrientation;
        TargetCameraSpec(String id, int width, int height, int sensorOrientation) {
            this.id = id; this.width = width; this.height = height;
            this.sensorOrientation = sensorOrientation;
        }
        String description() {
            return "目标相机 " + id + " · 原相机取景 " + width + "×" + height +
                    " · Sensor " + sensorOrientation + "°";
        }
    }
    private static final class PreviewResult extends BackendClient.Result {
        final Bitmap bitmap;
        PreviewResult(Bitmap bitmap) { super(0, "preview"); this.bitmap = bitmap; }
    }
    private static final class RoutesResult extends BackendClient.Result {
        final Map<String, String> routes;
        final List<Provider> providers;
        RoutesResult(Map<String, String> routes, List<Provider> providers) {
            super(0, "routes"); this.routes = routes; this.providers = providers;
        }
    }
    private static final class RouteSetupResult extends BackendClient.Result {
        final Map<String, String> routes;
        final List<Provider> providers;
        final List<AppEntry> apps;
        RouteSetupResult(Map<String, String> routes, List<Provider> providers, List<AppEntry> apps) {
            super(0, "route-setup");
            this.routes = routes; this.providers = providers; this.apps = apps;
        }
    }
    private static final class AppEntry {
        final String label, packageName;
        final boolean installed;
        AppEntry(String label, String packageName, boolean installed) {
            this.label = label; this.packageName = packageName; this.installed = installed;
        }
    }
    private final class AppPickerAdapter extends BaseAdapter {
        private final List<AppEntry> apps;
        AppPickerAdapter(List<AppEntry> apps) { this.apps = apps; }
        @Override public int getCount() { return apps.size(); }
        @Override public Object getItem(int position) { return apps.get(position); }
        @Override public long getItemId(int position) { return position; }
        @Override public View getView(int position, View reusable, ViewGroup parent) {
            LinearLayout row = reusable instanceof LinearLayout ? (LinearLayout) reusable : horizontal();
            row.removeAllViews();
            row.setOrientation(LinearLayout.HORIZONTAL);
            row.setGravity(Gravity.CENTER_VERTICAL);
            row.setPadding(dp(14), dp(12), dp(14), dp(12));
            row.setBackground(ripple(Color.WHITE, 16));
            row.setElevation(dp(1));
            AppEntry app = apps.get(position);
            ImageView icon = new ImageView(MainActivity.this);
            Drawable drawable = appIcons.get(app.packageName);
            if (drawable == null) {
                try { drawable = getPackageManager().getApplicationIcon(app.packageName); }
                catch (PackageManager.NameNotFoundException ignored) { }
                if (drawable != null) appIcons.put(app.packageName, drawable);
            }
            icon.setImageDrawable(drawable);
            row.addView(icon, new LinearLayout.LayoutParams(dp(44), dp(44)));
            LinearLayout labels = vertical();
            TextView title = text(app.label, 15, 0xff111827);
            title.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
            title.setSingleLine(true);
            title.setEllipsize(TextUtils.TruncateAt.END);
            labels.addView(title);
            TextView packageName = text(app.packageName, 11, 0xff64748b);
            packageName.setSingleLine(true);
            packageName.setEllipsize(TextUtils.TruncateAt.MIDDLE);
            labels.addView(packageName, matchWrapMargins(0, 3, 0, 0));
            row.addView(labels, weightedMargins(12, 0, 8, 0));
            TextView chevron = text("›", 28, 0xff94a3b8);
            chevron.setGravity(Gravity.CENTER);
            row.addView(chevron, new LinearLayout.LayoutParams(dp(24), dp(44)));
            return row;
        }
    }
}
