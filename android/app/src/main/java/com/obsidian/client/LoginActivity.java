package com.obsidian.client;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;

/**
 * Legacy entry point. Sign-in lives in {@link LauncherActivity} now.
 */
public class LoginActivity extends Activity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Intent intent = new Intent(this, LauncherActivity.class);
        intent.putExtra(LauncherActivity.EXTRA_OPEN_SIGNIN, true);
        intent.addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP | Intent.FLAG_ACTIVITY_SINGLE_TOP);
        startActivity(intent);
        finish();
    }
}
