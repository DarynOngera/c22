package com.telemetry;

import android.content.Context;

/** Minimal JNI surface for the native telemetry engine. */
public final class Telemetry {
    public static final String DEFAULT_ENDPOINT = "http://10.0.2.2:8080/telemetry";

    static {
        System.loadLibrary("telemetry");
    }

    private Telemetry() {
    }

    public static native void attach(Context context, String deviceId, String endpoint);

    public static native void start();

    public static native void stop();

    public static native void flush();

    public static native void locationUpdate(double lat, double lng, float accuracy,
                                            String provider, long fixMillis);
}