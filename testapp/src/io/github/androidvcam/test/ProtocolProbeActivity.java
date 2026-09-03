package io.github.androidvcam.test;

import android.Manifest;
import android.app.Activity;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.hardware.Camera;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CameraDevice;
import android.hardware.camera2.CameraManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import android.view.Gravity;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.util.Arrays;
import java.util.Set;

/** Exercises public Camera1, Camera2, and NDK entry points for protocol evidence. */
public final class ProtocolProbeActivity extends Activity {
    public static final String ACTION_RUN_PROTOCOL_PROBE =
            "io.github.androidvcam.test.RUN_PROTOCOL_PROBE";
    private static final int CAMERA_PERMISSION_REQUEST = 2101;
    private static final String INVALID_CAMERA_ID = "__vcam_protocol_probe_invalid__";

    private static boolean nativeProbeAvailable;
    static {
        try {
            System.loadLibrary("vcam_protocol_probe");
            nativeProbeAvailable = true;
        } catch (Throwable ignored) {
            nativeProbeAvailable = false;
        }
    }

    private TextView status;
    private Button start;
    private HandlerThread probeThread;
    private Handler probeHandler;
    private volatile CameraDevice camera2Device;
    private volatile Camera camera1Device;
    private volatile boolean running;
    private volatile boolean destroyed;
    private boolean autoStartPending;
    private final StringBuilder report = new StringBuilder();

    private static native String runNativeCameraManagerProbe();

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        setTitle("VCAM 协议自检");

        LinearLayout body = new LinearLayout(this);
        body.setOrientation(LinearLayout.VERTICAL);
        body.setPadding(dp(20), dp(20), dp(20), dp(20));
        body.setBackgroundColor(0xff0b1220);

        TextView title = new TextView(this);
        title.setText("CameraService 协议自检");
        title.setTextColor(Color.WHITE);
        title.setTextSize(22);
        body.addView(title, matchWrap(0, 0, 0, 8));

        TextView detail = new TextView(this);
        detail.setText("依次调用普通 Camera1、Camera2 和官方 NDK 相机接口。仅短暂打开后置相机，不修改路由配置。");
        detail.setTextColor(0xffa8b3c7);
        detail.setTextSize(14);
        body.addView(detail, matchWrap(0, 0, 0, 18));

        start = new Button(this);
        start.setText("开始自检");
        start.setOnClickListener(view -> startWhenPermitted());
        body.addView(start, matchWrap(0, 0, 0, 14));

        status = new TextView(this);
        status.setText("等待开始");
        status.setTextColor(0xffdbeafe);
        status.setTextSize(14);
        status.setGravity(Gravity.START);
        status.setTextIsSelectable(true);
        body.addView(status, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.addView(body);
        setContentView(scroll);

        autoStartPending = ACTION_RUN_PROTOCOL_PROBE.equals(
                getIntent().getAction());
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (probeThread == null) {
            probeThread = new HandlerThread("vcam-protocol-probe");
            probeThread.start();
            probeHandler = new Handler(probeThread.getLooper());
        }
        if (autoStartPending) {
            autoStartPending = false;
            startWhenPermitted();
        }
    }

    @Override
    protected void onDestroy() {
        destroyed = true;
        closeProbeCameras();
        if (probeThread != null) {
            probeThread.quitSafely();
            probeThread = null;
            probeHandler = null;
        }
        super.onDestroy();
    }

    private void startWhenPermitted() {
        if (running) return;
        if (checkSelfPermission(Manifest.permission.CAMERA) !=
                PackageManager.PERMISSION_GRANTED) {
            requestPermissions(
                    new String[]{Manifest.permission.CAMERA},
                    CAMERA_PERMISSION_REQUEST);
            return;
        }
        if (probeHandler == null) {
            showImmediate("自检线程尚未就绪，请重试");
            return;
        }
        running = true;
        start.setEnabled(false);
        report.setLength(0);
        append("开始公共 API 覆盖测试…");
        probeHandler.post(this::runSynchronousProbe);
    }

    private void runSynchronousProbe() {
        try {
            final CameraManager manager = getSystemService(CameraManager.class);
            if (manager == null) {
                finishProbe("CameraManager 不可用");
                return;
            }

            String[] ids = manager.getCameraIdList();
            append("Camera2 列表：" + Arrays.toString(ids));
            if (ids.length == 0) {
                finishProbe("未发现可见相机");
                return;
            }

            if (nativeProbeAvailable) {
                try {
                    append(runNativeCameraManagerProbe());
                } catch (Throwable error) {
                    append("NDK 探测失败：" + shortError(error));
                }
            } else {
                append("NDK 探测库不可用");
            }

            if (Build.VERSION.SDK_INT >= 30) {
                try {
                    Set<Set<String>> combinations = manager.getConcurrentCameraIds();
                    append("并发组合查询：" + combinations.size() + " 组");
                } catch (Throwable error) {
                    append("并发组合查询失败：" + shortError(error));
                }
            } else {
                append("并发组合查询：Android 10 不提供该接口，已跳过");
            }

            submitExpectedTorchRejections(manager);
            probeCamera1Back();
            if (destroyed) return;

            final String backCameraId = findCamera2Back(manager, ids);
            if (backCameraId == null) {
                finishProbe("没有后置 Camera2 设备；为避免升起前摄，未执行打开测试");
                return;
            }
            probeHandler.postDelayed(
                    () -> openCamera2Back(manager, backCameraId), 350);
        } catch (Throwable error) {
            finishProbe("自检中止：" + shortError(error));
        }
    }

    private void submitExpectedTorchRejections(CameraManager manager) {
        try {
            manager.setTorchMode(INVALID_CAMERA_ID, false);
            append("Torch 开关事务：服务意外接受了无效 ID");
        } catch (Throwable expected) {
            append("Torch 开关事务：已提交（无效 ID 按预期拒绝）");
        }
        if (Build.VERSION.SDK_INT < 33) return;
        try {
            manager.turnOnTorchWithStrengthLevel(INVALID_CAMERA_ID, 1);
            append("Torch 亮度设置事务：服务意外接受了无效 ID");
        } catch (Throwable expected) {
            append("Torch 亮度设置事务：已提交（按预期拒绝）");
        }
        try {
            manager.getTorchStrengthLevel(INVALID_CAMERA_ID);
            append("Torch 亮度查询事务：服务意外接受了无效 ID");
        } catch (Throwable expected) {
            append("Torch 亮度查询事务：已提交（按预期拒绝）");
        }
    }

    @SuppressWarnings("deprecation")
    private void probeCamera1Back() {
        int selected = -1;
        try {
            Camera.CameraInfo info = new Camera.CameraInfo();
            for (int index = 0; index < Camera.getNumberOfCameras(); ++index) {
                Camera.getCameraInfo(index, info);
                if (info.facing == Camera.CameraInfo.CAMERA_FACING_BACK) {
                    selected = index;
                    break;
                }
            }
            if (selected < 0) {
                append("Camera1：没有后置设备，已跳过");
                return;
            }
            camera1Device = Camera.open(selected);
            camera1Device.getParameters();
            append("Camera1 后置设备 " + selected + "：打开并关闭成功");
        } catch (Throwable error) {
            append("Camera1 打开失败：" + shortError(error));
        } finally {
            Camera active = camera1Device;
            camera1Device = null;
            if (active != null) active.release();
        }
    }

    private String findCamera2Back(CameraManager manager, String[] ids) {
        for (String id : ids) {
            try {
                Integer facing = manager.getCameraCharacteristics(id).get(
                        CameraCharacteristics.LENS_FACING);
                if (facing != null &&
                    facing == CameraCharacteristics.LENS_FACING_BACK) {
                    return id;
                }
            } catch (Throwable error) {
                append("读取相机 " + id + " 规格失败：" + shortError(error));
            }
        }
        return null;
    }

    private void openCamera2Back(CameraManager manager, String cameraId) {
        if (destroyed) return;
        try {
            append("正在短暂打开后置 Camera2 设备 " + cameraId + "…");
            manager.openCamera(cameraId, new CameraDevice.StateCallback() {
                @Override public void onOpened(CameraDevice device) {
                    camera2Device = device;
                    append("Camera2 后置设备 " + cameraId + "：打开并关闭成功");
                    device.close();
                    camera2Device = null;
                    finishProbe("公共协议调用已完成，请刷新 Manager 状态查看证据判定");
                }

                @Override public void onDisconnected(CameraDevice device) {
                    device.close();
                    camera2Device = null;
                    finishProbe("Camera2 设备在自检时断开");
                }

                @Override public void onError(CameraDevice device, int error) {
                    device.close();
                    camera2Device = null;
                    finishProbe("Camera2 打开错误：" + error);
                }
            }, probeHandler);
        } catch (Throwable error) {
            finishProbe("Camera2 打开失败：" + shortError(error));
        }
    }

    private void closeProbeCameras() {
        CameraDevice activeCamera2 = camera2Device;
        camera2Device = null;
        if (activeCamera2 != null) activeCamera2.close();
        Camera activeCamera1 = camera1Device;
        camera1Device = null;
        if (activeCamera1 != null) activeCamera1.release();
    }

    private void append(String line) {
        if (destroyed) return;
        synchronized (report) {
            if (report.length() > 0) report.append('\n');
            report.append("• ").append(line);
            showImmediate(report.toString());
        }
    }

    private void finishProbe(String result) {
        if (destroyed) return;
        append(result);
        running = false;
        runOnUiThread(() -> start.setEnabled(true));
    }

    private void showImmediate(String message) {
        runOnUiThread(() -> status.setText(message));
    }

    private String shortError(Throwable error) {
        String message = error.getMessage();
        return error.getClass().getSimpleName() +
                (message == null || message.isEmpty() ? "" : " · " + message);
    }

    @Override
    public void onRequestPermissionsResult(
            int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode != CAMERA_PERMISSION_REQUEST) return;
        if (grantResults.length > 0 &&
            grantResults[0] == PackageManager.PERMISSION_GRANTED) {
            startWhenPermitted();
        } else {
            showImmediate("需要相机权限才能运行协议自检");
        }
    }

    private LinearLayout.LayoutParams matchWrap(
            int left, int top, int right, int bottom) {
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        params.setMargins(dp(left), dp(top), dp(right), dp(bottom));
        return params;
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
}
