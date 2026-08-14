package io.github.androidvcam.test;

import android.Manifest;
import android.app.Activity;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.ImageFormat;
import android.graphics.SurfaceTexture;
import android.hardware.camera2.CameraAccessException;
import android.hardware.camera2.CameraCaptureSession;
import android.hardware.camera2.CameraDevice;
import android.hardware.camera2.CameraManager;
import android.hardware.camera2.CaptureRequest;
import android.media.Image;
import android.media.ImageReader;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import android.view.Gravity;
import android.view.Surface;
import android.view.TextureView;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.TextView;

import java.nio.ByteBuffer;
import java.util.Arrays;

/** Ordinary, root-free Camera2 client used for end-to-end validation. */
public final class CameraPreviewActivity extends Activity {
    private static final int CAMERA_PERMISSION_REQUEST = 2001;
    private TextureView preview;
    private TextView status;
    private HandlerThread cameraThread;
    private Handler cameraHandler;
    private CameraDevice camera;
    private CameraCaptureSession session;
    private ImageReader analysisReader;
    private Surface previewSurface;
    private int analyzedFrames;
    private String targetCameraId = "0";
    private boolean singleStream;
    private Button switchCamera;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        String requestedCamera = getIntent().getStringExtra("camera_id");
        if ("0".equals(requestedCamera) || "1".equals(requestedCamera)) {
            targetCameraId = requestedCamera;
        }
        singleStream = getIntent().getBooleanExtra("single_stream", false);
        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Color.BLACK);
        preview = new TextureView(this);
        root.addView(preview, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        status = new TextView(this);
        status.setText("正在准备标准 Camera2 预览…");
        status.setTextColor(Color.WHITE);
        status.setTextSize(15);
        status.setBackgroundColor(0x99000000);
        status.setPadding(dp(14), dp(10), dp(14), dp(10));
        root.addView(status, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT,
                Gravity.TOP));

        switchCamera = new Button(this);
        switchCamera.setText("切换到相机 " + ("0".equals(targetCameraId) ? "1" : "0"));
        switchCamera.setOnClickListener(view -> switchTargetCamera());
        FrameLayout.LayoutParams switchParams = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT,
                Gravity.BOTTOM | Gravity.END);
        switchParams.setMargins(dp(12), dp(12), dp(12), dp(22));
        root.addView(switchCamera, switchParams);
        setContentView(root);

        preview.setSurfaceTextureListener(new TextureView.SurfaceTextureListener() {
            @Override public void onSurfaceTextureAvailable(
                    SurfaceTexture surface, int width, int height) { openCameraIfPermitted(); }
            @Override public void onSurfaceTextureSizeChanged(
                    SurfaceTexture surface, int width, int height) { }
            @Override public boolean onSurfaceTextureDestroyed(SurfaceTexture surface) {
                closeCamera(); return true;
            }
            @Override public void onSurfaceTextureUpdated(SurfaceTexture surface) { }
        });
    }

    @Override protected void onResume() {
        super.onResume();
        cameraThread = new HandlerThread("vcam-preview");
        cameraThread.start();
        cameraHandler = new Handler(cameraThread.getLooper());
        if (preview.isAvailable()) openCameraIfPermitted();
    }

    @Override protected void onPause() {
        closeCamera();
        if (cameraThread != null) {
            cameraThread.quitSafely();
            try { cameraThread.join(); } catch (InterruptedException ignored) {
                Thread.currentThread().interrupt();
            }
            cameraThread = null;
            cameraHandler = null;
        }
        super.onPause();
    }

    private void openCameraIfPermitted() {
        if (checkSelfPermission(Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{Manifest.permission.CAMERA}, CAMERA_PERMISSION_REQUEST);
            return;
        }
        try {
            CameraManager manager = getSystemService(CameraManager.class);
            String[] ids = manager.getCameraIdList();
            if (ids.length == 0) { showStatus("没有发现摄像头设备"); return; }
            if (!Arrays.asList(ids).contains(targetCameraId)) targetCameraId = ids[0];
            showStatus("正在打开标准 Camera2 设备 " + targetCameraId + "…");
            manager.openCamera(targetCameraId, cameraStateCallback, cameraHandler);
        } catch (Exception error) { showStatus("打开失败：" + error); }
    }

    private void switchTargetCamera() {
        targetCameraId = "0".equals(targetCameraId) ? "1" : "0";
        switchCamera.setText("切换到相机 " + ("0".equals(targetCameraId) ? "1" : "0"));
        closeCamera();
        openCameraIfPermitted();
    }

    private final CameraDevice.StateCallback cameraStateCallback =
            new CameraDevice.StateCallback() {
        @Override public void onOpened(CameraDevice device) {
            camera = device; createPreviewSession();
        }
        @Override public void onDisconnected(CameraDevice device) {
            showStatus("摄像头已断开"); device.close(); if (camera == device) camera = null;
        }
        @Override public void onError(CameraDevice device, int error) {
            showStatus("Camera2 设备错误：" + error); device.close();
            if (camera == device) camera = null;
        }
    };

    private void createPreviewSession() {
        CameraDevice device = camera;
        SurfaceTexture texture = preview.getSurfaceTexture();
        if (device == null || texture == null) return;
        texture.setDefaultBufferSize(1280, 720);
        previewSurface = new Surface(texture);
        if (singleStream) {
            createSingleStreamSession(device);
            return;
        }
        analysisReader = ImageReader.newInstance(640, 480, ImageFormat.YUV_420_888, 2);
        analysisReader.setOnImageAvailableListener(this::analyzeFrame, cameraHandler);
        try {
            CaptureRequest.Builder request = device.createCaptureRequest(
                    CameraDevice.TEMPLATE_PREVIEW);
            request.addTarget(previewSurface);
            request.addTarget(analysisReader.getSurface());
            device.createCaptureSession(Arrays.asList(
                    previewSurface, analysisReader.getSurface()),
                    new CameraCaptureSession.StateCallback() {
                @Override public void onConfigured(CameraCaptureSession configured) {
                    if (camera == null) { configured.close(); return; }
                    session = configured;
                    try {
                        configured.setRepeatingRequest(request.build(), null, cameraHandler);
                        showStatus("Camera2 双流预览运行中，正在分析 YUV 帧…");
                    } catch (CameraAccessException error) {
                        showStatus("启动预览失败：" + error);
                    }
                }
                @Override public void onConfigureFailed(CameraCaptureSession failed) {
                    showStatus("Camera2 流配置失败");
                }
            }, cameraHandler);
        } catch (Exception error) { showStatus("创建预览失败：" + error); }
    }

    private void createSingleStreamSession(CameraDevice device) {
        try {
            CaptureRequest.Builder request = device.createCaptureRequest(
                    CameraDevice.TEMPLATE_PREVIEW);
            request.addTarget(previewSurface);
            device.createCaptureSession(Arrays.asList(previewSurface),
                    new CameraCaptureSession.StateCallback() {
                @Override public void onConfigured(CameraCaptureSession configured) {
                    if (camera == null) { configured.close(); return; }
                    session = configured;
                    try {
                        configured.setRepeatingRequest(request.build(), null, cameraHandler);
                        showStatus("Camera2 单流预览运行中");
                    } catch (CameraAccessException error) {
                        showStatus("启动预览失败：" + error);
                    }
                }
                @Override public void onConfigureFailed(CameraCaptureSession failed) {
                    showStatus("Camera2 单流配置失败");
                }
            }, cameraHandler);
        } catch (Exception error) { showStatus("创建单流预览失败：" + error); }
    }

    private void closeCamera() {
        CameraCaptureSession activeSession = session; session = null;
        if (activeSession != null) activeSession.close();
        CameraDevice activeCamera = camera; camera = null;
        if (activeCamera != null) activeCamera.close();
        ImageReader reader = analysisReader; analysisReader = null;
        if (reader != null) reader.close();
        Surface surface = previewSurface; previewSurface = null;
        if (surface != null) surface.release();
        analyzedFrames = 0;
    }

    private void analyzeFrame(ImageReader reader) {
        Image image = null;
        try {
            image = reader.acquireLatestImage();
            if (image == null) return;
            Image.Plane yPlane = image.getPlanes()[0];
            ByteBuffer y = yPlane.getBuffer();
            int rowStride = yPlane.getRowStride();
            int pixelStride = yPlane.getPixelStride();
            long sum = 0; int count = 0; int minimum = 255; int maximum = 0;
            for (int row = 0; row < image.getHeight(); row += 16) {
                for (int column = 0; column < image.getWidth(); column += 16) {
                    int index = row * rowStride + column * pixelStride;
                    if (index >= y.limit()) continue;
                    int value = y.get(index) & 0xff;
                    sum += value; ++count;
                    minimum = Math.min(minimum, value); maximum = Math.max(maximum, value);
                }
            }
            int frame = ++analyzedFrames;
            if (count > 0 && (frame == 1 || frame % 15 == 0)) {
                showStatus("Camera2 双流预览运行中 · 约 30 fps\nYUV 帧 " + frame +
                        "：亮度 avg=" + (sum / count) + "，min=" + minimum +
                        "，max=" + maximum);
            }
        } catch (Exception error) { showStatus("YUV 帧分析失败：" + error); }
        finally { if (image != null) image.close(); }
    }

    private void showStatus(String message) { runOnUiThread(() -> status.setText(message)); }

    @Override public void onRequestPermissionsResult(
            int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode != CAMERA_PERMISSION_REQUEST) return;
        if (grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
            openCameraIfPermitted();
        } else { showStatus("需要相机权限才能执行标准 Camera2 测试"); }
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
}
