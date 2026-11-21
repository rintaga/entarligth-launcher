package com.entarligth.launcher;

public final class NativeBridge {
    static { System.loadLibrary("luminacore"); }
    private NativeBridge() {}

    /** Blocking. Panggil dari thread background, bukan UI thread. */
    public static native int launch(String javaHome, String workDir, String logPath, String[] args);
}
