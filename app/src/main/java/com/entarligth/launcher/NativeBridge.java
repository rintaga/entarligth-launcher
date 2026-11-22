package com.entarligth.launcher;

public final class NativeBridge {
    static { System.loadLibrary("luminacore"); }
    private NativeBridge() {}

    /** Blocking. Panggil dari thread background, bukan UI thread. */
    public static native int launch(String javaHome, String workDir, String logPath, String[] args);

    // Input dari controller virtual (key = kode GLFW)
    public static native void sendKey(int glfwKey, boolean down);
    public static native void sendMouseButton(int button, boolean down);
    public static native void sendMouseMove(float dx, float dy);
    public static native void sendScroll(float dy);
    /** axes[6] = LX, LY, RX, RY, L2, R2; buttons = bitmask urutan tombol gamepad GLFW. */
    public static native void sendGamepad(float[] axes, int buttons);
}
