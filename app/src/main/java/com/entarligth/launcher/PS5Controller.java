package com.entarligth.launcher;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.View;

import java.util.LinkedHashMap;
import java.util.Map;

/**
 * Controller virtual bergaya DualSense (PS5) di atas layar.
 * Output: event keyboard/mouse (GLFW key code) + state gamepad (urutan tombol GLFW),
 * dikirim lewat NativeBridge ke libluminacore.so.
 */
public class PS5Controller extends View {
    // GLFW key codes
    private static final int K_W = 87, K_A = 65, K_S = 83, K_D = 68, K_SPACE = 32,
            K_LSHIFT = 340, K_LCTRL = 341, K_E = 69, K_Q = 81, K_ESC = 256, K_TAB = 258,
            K_T = 84, K_F1 = 290, K_F2 = 291, K_F3 = 292, K_F5 = 294, K_SLASH = 47;
    private static final float LOOK_SPEED = 22f, DEAD = 0.12f, WASD = 0.4f;

    private interface Act { void run(boolean down); }

    private final class Btn {
        final String id; final boolean rect; final int padBit, padAxis; final Act act;
        float cx, cy, w, h; int pointer = -1; boolean down;
        Btn(String id, boolean rect, int padBit, int padAxis, Act act) {
            this.id = id; this.rect = rect; this.padBit = padBit; this.padAxis = padAxis; this.act = act;
        }
        boolean hit(float x, float y) {
            if (rect) return Math.abs(x - cx) <= w * 0.55f && Math.abs(y - cy) <= h * 0.55f;
            return Math.hypot(x - cx, y - cy) <= w * 0.55f;
        }
    }

    private static final class Stick { float cx, cy, r, x, y; int pointer = -1; }

    private final Map<String, Btn> btns = new LinkedHashMap<>();
    private final Stick ls = new Stick(), rs = new Stick();
    private final boolean[] wasd = new boolean[4];
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint line = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF rf = new RectF();
    private final Path path = new Path();
    private float unit = 40f;
    private boolean looping;

    public PS5Controller(Context c) {
        super(c);
        // Pemetaan: id, bentuk persegi?, bit gamepad GLFW, axis trigger, aksi keyboard/mouse
        add("L2", true, -1, 4, mouse(1));      // pakai / pasang blok (klik kanan)
        add("L1", true, 4, -1, scroll(-1f));   // hotbar kiri
        add("R2", true, -1, 5, mouse(0));      // serang / hancurkan (klik kiri)
        add("R1", true, 5, -1, scroll(1f));    // hotbar kanan
        add("UP", true, 11, -1, key(K_F5));    // ganti sudut kamera
        add("RIGHT", true, 12, -1, key(K_F3)); // debug
        add("DOWN", true, 13, -1, key(K_T));   // chat
        add("LEFT", true, 14, -1, key(K_F1));  // sembunyikan HUD
        add("TRI", false, 3, -1, key(K_Q));    // buang item
        add("CROSS", false, 0, -1, key(K_SPACE));   // lompat
        add("SQUARE", false, 2, -1, key(K_E));      // inventori
        add("CIRCLE", false, 1, -1, key(K_LSHIFT)); // jongkok
        add("TOUCHPAD", true, -1, -1, key(K_TAB));  // daftar pemain
        add("CREATE", true, 6, -1, key(K_F2));      // screenshot
        add("OPTIONS", true, 7, -1, key(K_ESC));    // pause
        add("PS", false, 8, -1, key(K_SLASH));      // buka command
        add("L3", false, 9, -1, key(K_LCTRL));      // lari
        add("R3", false, 10, -1, d -> { });
        text.setTextAlign(Paint.Align.CENTER);
        text.setColor(0xDDFFFFFF);
        line.setStyle(Paint.Style.STROKE);
        setFocusable(false);
    }

    private void add(String id, boolean rect, int bit, int axis, Act a) {
        btns.put(id, new Btn(id, rect, bit, axis, a));
    }
    private Act key(int k) { return d -> NativeBridge.sendKey(k, d); }
    private Act mouse(int b) { return d -> NativeBridge.sendMouseButton(b, d); }
    private Act scroll(float v) { return d -> { if (d) NativeBridge.sendScroll(v); }; }

    private void place(String id, float cx, float cy, float w, float h) {
        Btn b = btns.get(id);
        b.cx = cx; b.cy = cy; b.w = w; b.h = h;
    }

    @Override protected void onSizeChanged(int w, int h, int ow, int oh) {
        float s = h / 10f;
        unit = s;
        place("L2", w * 0.11f, s * 0.9f, s * 3.4f, s * 1.3f);
        place("L1", w * 0.11f, s * 2.5f, s * 3.4f, s * 1.1f);
        place("R2", w * 0.89f, s * 0.9f, s * 3.4f, s * 1.3f);
        place("R1", w * 0.89f, s * 2.5f, s * 3.4f, s * 1.1f);

        float dx = w * 0.14f, dy = h * 0.52f, o = s * 1.3f, k = s * 1.25f;
        place("UP", dx, dy - o, k, k);
        place("DOWN", dx, dy + o, k, k);
        place("LEFT", dx - o, dy, k, k);
        place("RIGHT", dx + o, dy, k, k);

        float fx = w * 0.86f, fy = h * 0.52f, fo = s * 1.5f, fd = s * 1.5f;
        place("TRI", fx, fy - fo, fd, fd);
        place("CROSS", fx, fy + fo, fd, fd);
        place("SQUARE", fx - fo, fy, fd, fd);
        place("CIRCLE", fx + fo, fy, fd, fd);

        place("TOUCHPAD", w * 0.5f, s * 1.3f, w * 0.2f, s * 2.2f);
        place("CREATE", w * 0.5f - w * 0.1f - s * 1.8f, s * 1.3f, s * 2.2f, s * 1.0f);
        place("OPTIONS", w * 0.5f + w * 0.1f + s * 1.8f, s * 1.3f, s * 2.2f, s * 1.0f);
        place("PS", w * 0.5f, h * 0.52f, s * 1.3f, s * 1.3f);

        ls.cx = w * 0.32f; ls.cy = h * 0.76f; ls.r = s * 1.7f;
        rs.cx = w * 0.68f; rs.cy = h * 0.76f; rs.r = s * 1.7f;
        place("L3", ls.cx, ls.cy - s * 2.7f, s * 1.0f, s * 1.0f);
        place("R3", rs.cx, rs.cy - s * 2.7f, s * 1.0f, s * 1.0f);
    }

    // ---------- input ----------

    @Override public boolean onTouchEvent(MotionEvent e) {
        int idx = e.getActionIndex();
        switch (e.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN:
                down(e.getPointerId(idx), e.getX(idx), e.getY(idx));
                break;
            case MotionEvent.ACTION_MOVE:
                for (int i = 0; i < e.getPointerCount(); i++)
                    move(e.getPointerId(i), e.getX(i), e.getY(i));
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
                up(e.getPointerId(idx));
                break;
            case MotionEvent.ACTION_CANCEL:
                releaseAll();
                break;
        }
        invalidate();
        return true;
    }

    private void down(int id, float x, float y) {
        for (Btn b : btns.values()) {
            if (b.pointer < 0 && b.hit(x, y)) { b.pointer = id; press(b, true); return; }
        }
        if (ls.pointer < 0 && Math.hypot(x - ls.cx, y - ls.cy) <= ls.r * 1.5f) {
            ls.pointer = id; updateStick(ls, x, y); return;
        }
        if (rs.pointer < 0 && Math.hypot(x - rs.cx, y - rs.cy) <= rs.r * 1.5f) {
            rs.pointer = id; updateStick(rs, x, y);
            if (!looping) { looping = true; postOnAnimation(tick); }
        }
    }

    private void move(int id, float x, float y) {
        if (ls.pointer == id) updateStick(ls, x, y);
        else if (rs.pointer == id) updateStick(rs, x, y);
    }

    private void up(int id) {
        for (Btn b : btns.values()) if (b.pointer == id) { b.pointer = -1; press(b, false); }
        if (ls.pointer == id) releaseStick(ls);
        if (rs.pointer == id) releaseStick(rs);
    }

    private void releaseAll() {
        for (Btn b : btns.values()) if (b.pointer >= 0) { b.pointer = -1; press(b, false); }
        if (ls.pointer >= 0) releaseStick(ls);
        if (rs.pointer >= 0) releaseStick(rs);
    }

    @Override protected void onVisibilityChanged(View v, int vis) {
        super.onVisibilityChanged(v, vis);
        if (vis != VISIBLE) releaseAll();
    }

    private void press(Btn b, boolean d) {
        if (b.down == d) return;
        b.down = d;
        b.act.run(d);
        if (d) performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP);
        pushPad();
    }

    private void updateStick(Stick s, float x, float y) {
        float dx = (x - s.cx) / s.r, dy = (y - s.cy) / s.r;
        float m = (float) Math.hypot(dx, dy);
        if (m > 1f) { dx /= m; dy /= m; }
        s.x = Math.abs(dx) < DEAD ? 0f : dx;
        s.y = Math.abs(dy) < DEAD ? 0f : dy;
        if (s == ls) updateWasd();
        pushPad();
    }

    private void releaseStick(Stick s) {
        s.pointer = -1; s.x = 0f; s.y = 0f;
        if (s == ls) updateWasd();
        pushPad();
    }

    private void updateWasd() {
        boolean[] n = {ls.y < -WASD, ls.x < -WASD, ls.y > WASD, ls.x > WASD};
        int[] k = {K_W, K_A, K_S, K_D};
        for (int i = 0; i < 4; i++) {
            if (n[i] != wasd[i]) { wasd[i] = n[i]; NativeBridge.sendKey(k[i], n[i]); }
        }
    }

    /** Stick kanan = kamera: kirim delta mouse tiap frame selama disentuh. */
    private final Runnable tick = new Runnable() {
        @Override public void run() {
            if (rs.pointer < 0) { looping = false; return; }
            if (rs.x != 0f || rs.y != 0f)
                NativeBridge.sendMouseMove(curve(rs.x) * LOOK_SPEED, curve(rs.y) * LOOK_SPEED);
            postOnAnimation(this);
        }
    };

    private static float curve(float v) { return v * Math.abs(v); }

    private void pushPad() {
        int mask = 0;
        float[] ax = {ls.x, ls.y, rs.x, rs.y, -1f, -1f};
        for (Btn b : btns.values()) {
            if (!b.down) continue;
            if (b.padBit >= 0) mask |= 1 << b.padBit;
            if (b.padAxis >= 0) ax[b.padAxis] = 1f;
        }
        NativeBridge.sendGamepad(ax, mask);
    }

    // ---------- gambar ----------

    @Override protected void onDraw(Canvas c) {
        text.setTextSize(unit * 0.5f);
        for (Btn b : btns.values()) drawBtn(c, b);
        drawStick(c, ls);
        drawStick(c, rs);
    }

    private void drawBtn(Canvas c, Btn b) {
        fill.setStyle(Paint.Style.FILL);
        fill.setColor(b.down ? 0xB4FFFFFF : 0x46FFFFFF);
        line.setColor(0xA0FFFFFF);
        line.setStrokeWidth(unit * 0.06f);
        if (b.rect) {
            rf.set(b.cx - b.w / 2, b.cy - b.h / 2, b.cx + b.w / 2, b.cy + b.h / 2);
            float rad = Math.min(b.w, b.h) * 0.3f;
            c.drawRoundRect(rf, rad, rad, fill);
            c.drawRoundRect(rf, rad, rad, line);
        } else {
            c.drawCircle(b.cx, b.cy, b.w / 2, fill);
            c.drawCircle(b.cx, b.cy, b.w / 2, line);
        }
        switch (b.id) {
            case "TRI": case "CROSS": case "SQUARE": case "CIRCLE":
                symbol(c, b.id, b.cx, b.cy, b.w * 0.25f); break;
            case "UP": arrow(c, b, 0f); break;
            case "RIGHT": arrow(c, b, 90f); break;
            case "DOWN": arrow(c, b, 180f); break;
            case "LEFT": arrow(c, b, 270f); break;
            case "TOUCHPAD": break;
            default: label(c, b.id, b.cx, b.cy);
        }
    }

    private void label(Canvas c, String s, float x, float y) {
        float ty = y - (text.ascent() + text.descent()) / 2f;
        c.drawText(s, x, ty, text);
    }

    private void arrow(Canvas c, Btn b, float angle) {
        float k = b.w * 0.25f;
        c.save();
        c.translate(b.cx, b.cy);
        c.rotate(angle);
        path.reset();
        path.moveTo(0, -k);
        path.lineTo(k * 0.8f, k * 0.6f);
        path.lineTo(-k * 0.8f, k * 0.6f);
        path.close();
        fill.setColor(0xDDFFFFFF);
        c.drawPath(path, fill);
        c.restore();
    }

    private void symbol(Canvas c, String id, float x, float y, float r) {
        line.setStrokeWidth(r * 0.2f);
        switch (id) {
            case "TRI":
                line.setColor(0xFF4CD9A4);
                path.reset();
                path.moveTo(x, y - r);
                path.lineTo(x + r * 0.87f, y + r * 0.5f);
                path.lineTo(x - r * 0.87f, y + r * 0.5f);
                path.close();
                c.drawPath(path, line);
                break;
            case "CIRCLE":
                line.setColor(0xFFFF5C6C);
                c.drawCircle(x, y, r * 0.9f, line);
                break;
            case "CROSS":
                line.setColor(0xFF6EA8FF);
                c.drawLine(x - r * 0.8f, y - r * 0.8f, x + r * 0.8f, y + r * 0.8f, line);
                c.drawLine(x - r * 0.8f, y + r * 0.8f, x + r * 0.8f, y - r * 0.8f, line);
                break;
            case "SQUARE":
                line.setColor(0xFFFF8CD0);
                c.drawRect(x - r * 0.8f, y - r * 0.8f, x + r * 0.8f, y + r * 0.8f, line);
                break;
        }
    }

    private void drawStick(Canvas c, Stick s) {
        fill.setStyle(Paint.Style.FILL);
        fill.setColor(0x28FFFFFF);
        line.setColor(0x90FFFFFF);
        line.setStrokeWidth(unit * 0.06f);
        c.drawCircle(s.cx, s.cy, s.r, fill);
        c.drawCircle(s.cx, s.cy, s.r, line);
        fill.setColor(s.pointer >= 0 ? 0xC8FFFFFF : 0x78FFFFFF);
        c.drawCircle(s.cx + s.x * s.r * 0.6f, s.cy + s.y * s.r * 0.6f, s.r * 0.55f, fill);
    }
}
