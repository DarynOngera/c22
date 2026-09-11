package com.telemetry;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.location.LocationManager;
import android.os.Build;
import android.os.IBinder;
import android.os.Looper;
import android.os.PowerManager;

/**
 * 24/7 foreground service that hosts the native telemetry engine.
 * Started with:
 *   adb shell am start-foreground-service -n com.telemetry/.TelemetryService \
 *     -e endpoint http://10.0.2.2:8080/telemetry
 */
public class TelemetryService extends Service {

    private static final String CHANNEL_ID = "telemetry";
    private static final int NOTIFICATION_ID = 1;
    private static final long LOCATION_MIN_TIME_MS = 30_000;
    private static final float LOCATION_MIN_DISTANCE_M = 0f;

    private TelemetryLocationListener locationListener;
    private PowerManager.WakeLock wakeLock;

    @Override
    public void onCreate() {
        super.onCreate();
        ensureNotificationChannel();
        Notification notification = buildNotification();
        if (Build.VERSION.SDK_INT >= 29) {
            try {
                startForeground(NOTIFICATION_ID, notification,
                        ServiceInfo.FOREGROUND_SERVICE_TYPE_LOCATION
                                | ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC);
            } catch (SecurityException e) {
                startForeground(NOTIFICATION_ID, notification,
                        ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC);
            }
        } else {
            startForeground(NOTIFICATION_ID, notification);
        }

        PowerManager pm = (PowerManager) getSystemService(Context.POWER_SERVICE);
        wakeLock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "telemetry:wakelock");
        wakeLock.acquire();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        String endpoint = Telemetry.DEFAULT_ENDPOINT;
        if (intent != null && intent.getStringExtra("endpoint") != null) {
            endpoint = intent.getStringExtra("endpoint");
        }
        Telemetry.attach(getApplicationContext(), TelemetryLocationListener.deviceId(this), endpoint);
        Telemetry.start();
        registerLocationUpdates();
        return START_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public void onDestroy() {
        super.onDestroy();
        LocationManager lm = (LocationManager) getSystemService(Context.LOCATION_SERVICE);
        if (locationListener != null) {
            lm.removeUpdates(locationListener);
        }
        Telemetry.stop();
        if (wakeLock != null && wakeLock.isHeld()) {
            wakeLock.release();
        }
    }

    private void registerLocationUpdates() {
        LocationManager lm = (LocationManager) getSystemService(Context.LOCATION_SERVICE);
        if (locationListener == null) {
            locationListener = new TelemetryLocationListener(this);
        }
        for (String provider : new String[]{LocationManager.GPS_PROVIDER,
                LocationManager.NETWORK_PROVIDER}) {
            try {
                if (lm.isProviderEnabled(provider)) {
                    lm.requestLocationUpdates(provider, LOCATION_MIN_TIME_MS,
                            LOCATION_MIN_DISTANCE_M, locationListener, Looper.getMainLooper());
                }
            } catch (SecurityException e) {
                // Location permission not granted; other telemetry still works.
            } catch (IllegalArgumentException e) {
                // Provider unavailable on this device.
            }
        }
    }

    private void ensureNotificationChannel() {
        if (Build.VERSION.SDK_INT >= 26) {
            NotificationManager nm = getSystemService(NotificationManager.class);
            NotificationChannel ch = new NotificationChannel(CHANNEL_ID, "Telemetry",
                    NotificationManager.IMPORTANCE_LOW);
            nm.createNotificationChannel(ch);
        }
    }

    private Notification buildNotification() {
        return new Notification.Builder(this)
                .setChannelId(CHANNEL_ID)
                .setSmallIcon(android.R.drawable.stat_notify_sync)
                .setContentTitle("Telemetry")
                .setContentText("Collecting device telemetry")
                .setOngoing(true)
                .setShowWhen(false)
                .build();
    }
}