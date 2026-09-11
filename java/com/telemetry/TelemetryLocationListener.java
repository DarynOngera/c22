package com.telemetry;

import android.content.Context;
import android.provider.Settings;

/**
 * Forwarded location fixes from LocationManager into the native engine.
 * A background telemetry service needs "Allow all the time" location access;
 * grant with:
 *   adb shell pm grant com.telemetry android.permission.ACCESS_FINE_LOCATION
 *   adb shell appops set com.telemetry android:fine_location allow
 */
public final class TelemetryLocationListener implements android.location.LocationListener {

    private final Context context;

    public TelemetryLocationListener(Context context) {
        this.context = context;
    }

    @Override
    public void onLocationChanged(android.location.Location location) {
        if (location == null) {
            return;
        }
        Telemetry.locationUpdate(location.getLatitude(), location.getLongitude(),
                location.getAccuracy(), location.getProvider(), location.getTime());
    }

    /** ANDROID_ID, stable per app-signing key + user. */
    public static String deviceId(Context context) {
        String id = Settings.Secure.getString(context.getContentResolver(),
                Settings.Secure.ANDROID_ID);
        return id != null ? id : "unknown";
    }
}