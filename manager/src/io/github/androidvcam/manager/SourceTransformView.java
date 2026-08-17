package io.github.androidvcam.manager;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Matrix;
import android.graphics.Paint;
import android.graphics.RectF;
import android.view.MotionEvent;
import android.view.ScaleGestureDetector;
import android.view.View;

/** Interactive source preview with a draggable target-camera viewport. */
final class SourceTransformView extends View {
    static final class Setting {
        int rotation;
        float scale = 1f;
        float centerX = .5f;
        float centerY = .5f;

        String encode() {
            return rotation + "," + Math.round(scale * 1000f) + "," +
                    Math.round(centerX * 1000f) + "," + Math.round(centerY * 1000f);
        }

        Setting copy() {
            Setting result = new Setting();
            result.rotation = rotation; result.scale = scale;
            result.centerX = centerX; result.centerY = centerY;
            return result;
        }

        static Setting decode(String encoded) {
            Setting result = new Setting();
            if (encoded == null) return result;
            String[] fields = encoded.split(",", -1);
            if (fields.length != 4) return result;
            try {
                int rotation = Integer.parseInt(fields[0]);
                if (rotation == 0 || rotation == 90 || rotation == 180 || rotation == 270) {
                    result.rotation = rotation;
                }
                result.scale = Math.max(.1f, Math.min(8f,
                        Integer.parseInt(fields[1]) / 1000f));
                result.centerX = Math.max(0f, Math.min(1f,
                        Integer.parseInt(fields[2]) / 1000f));
                result.centerY = Math.max(0f, Math.min(1f,
                        Integer.parseInt(fields[3]) / 1000f));
            } catch (NumberFormatException ignored) { }
            return result;
        }
    }

    private final Bitmap bitmap;
    private final Paint imagePaint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private final Paint shadePaint = new Paint();
    private final Paint framePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF frameRect = new RectF();
    private final ScaleGestureDetector scaleDetector;
    private Setting setting = new Setting();
    private float targetAspect = 3f / 4f;
    private float lastX;
    private float lastY;
    private float shownWidth;
    private float shownHeight;

    SourceTransformView(Context context, Bitmap bitmap) {
        super(context);
        this.bitmap = bitmap;
        setBackgroundColor(Color.BLACK);
        shadePaint.setColor(0x99000000);
        framePaint.setStyle(Paint.Style.STROKE);
        framePaint.setStrokeWidth(getResources().getDisplayMetrics().density * 2f);
        framePaint.setColor(Color.WHITE);
        scaleDetector = new ScaleGestureDetector(context,
                new ScaleGestureDetector.SimpleOnScaleGestureListener() {
            @Override public boolean onScale(ScaleGestureDetector detector) {
                setScale(setting.scale * detector.getScaleFactor());
                return true;
            }
        });
    }

    void setTargetAspect(int width, int height) {
        if (width > 0 && height > 0) targetAspect = (float) width / height;
        invalidate();
    }

    void setSetting(Setting value) { setting = value.copy(); invalidate(); }
    Setting getSetting() { return setting.copy(); }
    void setScale(float scale) { setting.scale = Math.max(.1f, Math.min(8f, scale)); invalidate(); }
    void rotateClockwise() { setting.rotation = (setting.rotation + 90) % 360; invalidate(); }

    @Override protected void onMeasure(int widthSpec, int heightSpec) {
        int width = MeasureSpec.getSize(widthSpec);
        int desired = (int)(getResources().getDisplayMetrics().density * 360);
        int height = resolveSize(desired, heightSpec);
        setMeasuredDimension(width, height);
    }

    @Override protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        float rotatedWidth = (setting.rotation == 90 || setting.rotation == 270)
                ? bitmap.getHeight() : bitmap.getWidth();
        float rotatedHeight = (setting.rotation == 90 || setting.rotation == 270)
                ? bitmap.getWidth() : bitmap.getHeight();

        float availableWidth = getWidth() * .88f;
        float availableHeight = getHeight() * .88f;
        float frameWidth;
        float frameHeight;
        if (availableWidth / availableHeight > targetAspect) {
            frameHeight = availableHeight; frameWidth = frameHeight * targetAspect;
        } else {
            frameWidth = availableWidth; frameHeight = frameWidth / targetAspect;
        }
        frameRect.set((getWidth() - frameWidth) / 2f, (getHeight() - frameHeight) / 2f,
                (getWidth() + frameWidth) / 2f, (getHeight() + frameHeight) / 2f);

        float cover = Math.max(frameWidth / rotatedWidth, frameHeight / rotatedHeight);
        float drawScale = cover * setting.scale;
        shownWidth = rotatedWidth * drawScale;
        shownHeight = rotatedHeight * drawScale;
        clampCenter(frameWidth, frameHeight);
        Matrix matrix = new Matrix();
        matrix.postTranslate(-setting.centerX * bitmap.getWidth(),
                -setting.centerY * bitmap.getHeight());
        matrix.postRotate(setting.rotation);
        matrix.postScale(drawScale, drawScale);
        matrix.postTranslate(frameRect.centerX(), frameRect.centerY());
        canvas.drawBitmap(bitmap, matrix, imagePaint);

        canvas.drawRect(0, 0, getWidth(), frameRect.top, shadePaint);
        canvas.drawRect(0, frameRect.bottom, getWidth(), getHeight(), shadePaint);
        canvas.drawRect(0, frameRect.top, frameRect.left, frameRect.bottom, shadePaint);
        canvas.drawRect(frameRect.right, frameRect.top, getWidth(), frameRect.bottom, shadePaint);
        canvas.drawRect(frameRect, framePaint);
        canvas.drawLine(frameRect.left + frameRect.width() / 3f, frameRect.top,
                frameRect.left + frameRect.width() / 3f, frameRect.bottom, framePaint);
        canvas.drawLine(frameRect.left + frameRect.width() * 2f / 3f, frameRect.top,
                frameRect.left + frameRect.width() * 2f / 3f, frameRect.bottom, framePaint);
        canvas.drawLine(frameRect.left, frameRect.top + frameRect.height() / 3f,
                frameRect.right, frameRect.top + frameRect.height() / 3f, framePaint);
        canvas.drawLine(frameRect.left, frameRect.top + frameRect.height() * 2f / 3f,
                frameRect.right, frameRect.top + frameRect.height() * 2f / 3f, framePaint);
    }

    @Override public boolean onTouchEvent(MotionEvent event) {
        scaleDetector.onTouchEvent(event);
        if (event.getActionMasked() == MotionEvent.ACTION_DOWN) {
            lastX = event.getX(); lastY = event.getY(); return true;
        }
        if (event.getActionMasked() == MotionEvent.ACTION_POINTER_UP) {
            int remaining = event.getActionIndex() == 0 ? 1 : 0;
            if (remaining < event.getPointerCount()) {
                lastX = event.getX(remaining); lastY = event.getY(remaining);
            }
            return true;
        }
        if (event.getActionMasked() == MotionEvent.ACTION_MOVE &&
                event.getPointerCount() == 1 && shownWidth > 0) {
            float[] rotated = originalToRotated(setting.centerX, setting.centerY, setting.rotation);
            // Dragging the preview right/down selects a source center left/up.
            rotated[0] -= (event.getX() - lastX) / shownWidth;
            rotated[1] -= (event.getY() - lastY) / shownHeight;
            float[] original = rotatedToOriginal(rotated[0], rotated[1], setting.rotation);
            setting.centerX = original[0]; setting.centerY = original[1];
            lastX = event.getX(); lastY = event.getY(); invalidate(); return true;
        }
        return event.getActionMasked() == MotionEvent.ACTION_UP || super.onTouchEvent(event);
    }

    private void clampCenter(float frameWidth, float frameHeight) {
        float[] rotated = originalToRotated(setting.centerX, setting.centerY, setting.rotation);
        float minimumX = shownWidth >= frameWidth ? frameWidth / (2f * shownWidth) : 0f;
        float minimumY = shownHeight >= frameHeight ? frameHeight / (2f * shownHeight) : 0f;
        rotated[0] = clamp(rotated[0], minimumX, 1f - minimumX);
        rotated[1] = clamp(rotated[1], minimumY, 1f - minimumY);
        float[] original = rotatedToOriginal(rotated[0], rotated[1], setting.rotation);
        setting.centerX = original[0]; setting.centerY = original[1];
    }

    private static float[] originalToRotated(float x, float y, int rotation) {
        if (rotation == 90) return new float[]{1f - y, x};
        if (rotation == 180) return new float[]{1f - x, 1f - y};
        if (rotation == 270) return new float[]{y, 1f - x};
        return new float[]{x, y};
    }

    private static float[] rotatedToOriginal(float x, float y, int rotation) {
        if (rotation == 90) return new float[]{y, 1f - x};
        if (rotation == 180) return new float[]{1f - x, 1f - y};
        if (rotation == 270) return new float[]{1f - y, x};
        return new float[]{x, y};
    }

    private static float clamp(float value, float minimum, float maximum) {
        return Math.max(minimum, Math.min(maximum, value));
    }
}
