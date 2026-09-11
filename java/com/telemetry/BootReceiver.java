package com.telemetry;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.os.Build;

/** Restarts the telemetry service after an OS reboot (permission granted via pm). */
public class BootReceiver extends BroadcastReceiver {

    @Override
    public void onReceive(Context context, Intent intent) {
        if (!Intent.ACTION_BOOT_COMPLETED.equals(intent.getAction())) {
            return;
        }
        try {
            Intent start = new Intent(context, TelemetryService.class);
            if (Build.VERSION.SDK_INT >= 26) {
                context.startForegroundService(start);
            } else {
                context.startService(start);
            }
        } catch (Exception e) {
            // App was force-stopped or not yet "launched"; user must start the
            // service once via adb shell am start-foreground-service.
        }
    }
}