package com.obsidian.client;

import android.app.Activity;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.ActivityInfo;
import android.os.Build;
import android.os.Bundle;
import android.text.InputType;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.SeekBar;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;
import android.widget.ViewFlipper;

import java.io.File;
import java.io.FileInputStream;
import java.nio.charset.StandardCharsets;

/**
 * Single landscape Obsidian shell: settings + Sign in. No separate vertical login activity.
 */
public class LauncherActivity extends Activity {
    public static final String EXTRA_OPEN_SIGNIN = "open_signin";

    private static final int PAGE_HOME = 0;
    private static final int PAGE_SIGNIN = 1;
    private static final int PAGE_GRAPHICS = 2;
    private static final int PAGE_AUDIO = 3;
    private static final int PAGE_CONTROLS = 4;
    private static final int PAGE_PATHS = 5;
    private static final int PAGE_ABOUT = 6;

    private SharedPreferences prefs;
    private ViewFlipper flipper;
    private Button[] navButtons;
    private boolean suppressPresetCallback;

    private EditText signinHost;
    private EditText signinPort;
    private EditText signinUser;
    private EditText signinPass;
    private TextView signinStatus;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        setContentView(R.layout.activity_launcher);

        prefs = ObsidianSettings.prefs(this);
        ObsidianSettings.applyDefaults(prefs);

        flipper = findViewById(R.id.content_flipper);
        navButtons = new Button[] {
                findViewById(R.id.nav_home),
                findViewById(R.id.nav_signin),
                findViewById(R.id.nav_graphics),
                findViewById(R.id.nav_audio),
                findViewById(R.id.nav_controls),
                findViewById(R.id.nav_paths),
                findViewById(R.id.nav_about)
        };

        for (int i = 0; i < navButtons.length; i++) {
            final int page = i;
            navButtons[i].setOnClickListener(v -> showPage(page));
        }

        findViewById(R.id.btn_play).setOnClickListener(v -> playFromLauncher());
        findViewById(R.id.home_open_graphics).setOnClickListener(v -> showPage(PAGE_GRAPHICS));
        findViewById(R.id.home_open_paths).setOnClickListener(v -> showPage(PAGE_SIGNIN));

        bindGraphics();
        bindAudio();
        bindControls();
        bindPaths();
        bindSignIn();
        bindAbout();
        refreshHome();

        if (getIntent() != null && getIntent().getBooleanExtra(EXTRA_OPEN_SIGNIN, false)) {
            showPage(PAGE_SIGNIN);
        } else {
            showPage(PAGE_HOME);
        }
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        if (intent != null && intent.getBooleanExtra(EXTRA_OPEN_SIGNIN, false)) {
            showPage(PAGE_SIGNIN);
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
        hideSystemUi();
        refreshHome();
        refreshPathsStatus();
        showPendingAuthError();
    }

    private void showPage(int page) {
        flipper.setDisplayedChild(page);
        for (int i = 0; i < navButtons.length; i++) {
            navButtons[i].setSelected(i == page);
        }
    }

    private void bindSignIn() {
        signinHost = findViewById(R.id.signin_host);
        signinPort = findViewById(R.id.signin_port);
        signinUser = findViewById(R.id.signin_username);
        signinPass = findViewById(R.id.signin_password);
        signinStatus = findViewById(R.id.signin_status);
        CheckBox showPassword = findViewById(R.id.signin_show_password);

        signinHost.setText(prefs.getString(ObsidianSettings.KEY_REALM_HOST, "logon.retro-wow.org"));
        signinPort.setText(Integer.toString(prefs.getInt(ObsidianSettings.KEY_REALM_PORT, 3724)));
        signinUser.setText(prefs.getString(ObsidianSettings.KEY_ACCOUNT_USER, ""));
        signinPass.setText(prefs.getString(ObsidianSettings.KEY_ACCOUNT_PASS, ""));

        findViewById(R.id.signin_preset_retro).setOnClickListener(v -> {
            signinHost.setText("logon.retro-wow.org");
            signinPort.setText("3724");
        });
        findViewById(R.id.signin_preset_twinstar).setOnClickListener(v -> {
            signinHost.setText("login.twinstar-wow.com");
            signinPort.setText("3724");
        });

        showPassword.setOnCheckedChangeListener((buttonView, isChecked) -> {
            int type = isChecked
                    ? InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD
                    : InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD;
            signinPass.setInputType(type);
            signinPass.setSelection(signinPass.getText().length());
        });
    }

    /** Single entry into the client — saves Sign in fields, then boots WoWee. */
    private void playFromLauncher() {
        // Ensure Sign-in widgets exist even if user never opened that page.
        if (signinHost == null) bindSignIn();

        String hostValue = sanitizeHost(signinHost.getText().toString());
        signinHost.setText(hostValue);
        String userValue = signinUser.getText().toString().trim();
        String passValue = signinPass.getText().toString();
        int portValue = 3724;
        try {
            portValue = Integer.parseInt(signinPort.getText().toString().trim());
        } catch (NumberFormatException ignored) {
        }
        if (portValue < 1 || portValue > 65535) portValue = 3724;

        if (hostValue.isEmpty() || hostValue.endsWith(".rg")
                || userValue.isEmpty() || passValue.isEmpty()) {
            showPage(PAGE_SIGNIN);
            if (hostValue.isEmpty()) {
                showSignInError("Enter a realm hostname (RetroWoW: logon.retro-wow.org).");
            } else if (hostValue.endsWith(".rg")) {
                showSignInError("Hostname ends in .rg — RetroWoW uses .org (logon.retro-wow.org).");
            } else if (userValue.isEmpty()) {
                showSignInError("Enter your account username, then press Play.");
            } else {
                showSignInError("Enter your account password, then press Play.");
            }
            return;
        }

        prefs.edit()
                .putString(ObsidianSettings.KEY_REALM_HOST, hostValue)
                .putInt(ObsidianSettings.KEY_REALM_PORT, portValue)
                .putString(ObsidianSettings.KEY_ACCOUNT_USER, userValue)
                .putString(ObsidianSettings.KEY_ACCOUNT_PASS, passValue)
                .remove(ObsidianSettings.KEY_AUTH_ERROR)
                .apply();

        try {
            ObsidianSettings.writeNativeSettings(this);
        } catch (Exception e) {
            showPage(PAGE_SIGNIN);
            showSignInError("Failed to write settings.cfg: " + e.getMessage());
            return;
        }

        clearAuthErrorFile();
        if (signinStatus != null) signinStatus.setVisibility(View.GONE);

        Intent intent = new Intent(this, MainActivity.class);
        intent.putExtra(MainActivity.EXTRA_AUTO_LOGIN, true);
        startActivity(intent);
    }

    /** Normalize pasted realmlist lines and common RetroWoW typos. */
    static String sanitizeHost(String raw) {
        String cleaned = raw == null ? "" : raw.trim();
        String lower = cleaned.toLowerCase();
        final String prefix = "set realmlist ";
        if (lower.startsWith(prefix)) {
            cleaned = cleaned.substring(prefix.length()).trim();
            if ((cleaned.startsWith("\"") && cleaned.endsWith("\""))
                    || (cleaned.startsWith("'") && cleaned.endsWith("'"))) {
                cleaned = cleaned.substring(1, cleaned.length() - 1).trim();
            }
            lower = cleaned.toLowerCase();
        }
        int colon = cleaned.lastIndexOf(':');
        if (colon > 0) {
            String maybePort = cleaned.substring(colon + 1);
            boolean digits = !maybePort.isEmpty();
            for (int i = 0; i < maybePort.length(); i++) {
                if (!Character.isDigit(maybePort.charAt(i))) {
                    digits = false;
                    break;
                }
            }
            if (digits) {
                cleaned = cleaned.substring(0, colon);
                lower = cleaned.toLowerCase();
            }
        }
        // Common mistype: logon.retro-wow.rg → .org
        if (lower.equals("logon.retro-wow.rg") || lower.equals("logon.retro-wow.com")) {
            cleaned = "logon.retro-wow.org";
        }
        return cleaned;
    }

    private void showSignInError(String message) {
        signinStatus.setText(message);
        signinStatus.setVisibility(View.VISIBLE);
        Toast.makeText(this, message, Toast.LENGTH_LONG).show();
    }

    private void showPendingAuthError() {
        String fromPrefs = prefs.getString(ObsidianSettings.KEY_AUTH_ERROR, "");
        String fromFile = readAuthErrorFile();
        String message = (fromFile != null && !fromFile.isEmpty()) ? fromFile : fromPrefs;
        if (message == null || message.isEmpty()) return;
        showPage(PAGE_SIGNIN);
        showSignInError(message);
        prefs.edit().remove(ObsidianSettings.KEY_AUTH_ERROR).apply();
        clearAuthErrorFile();
    }

    private File authErrorFile() {
        return new File(ObsidianSettings.configDir(this), "last_auth_error.txt");
    }

    private String readAuthErrorFile() {
        File file = authErrorFile();
        if (!file.isFile()) return "";
        try (FileInputStream in = new FileInputStream(file)) {
            byte[] buf = new byte[(int) Math.min(file.length(), 2048)];
            int n = in.read(buf);
            if (n <= 0) return "";
            return new String(buf, 0, n, StandardCharsets.UTF_8).trim();
        } catch (Exception e) {
            return "";
        }
    }

    private void clearAuthErrorFile() {
        //noinspection ResultOfMethodCallIgnored
        authErrorFile().delete();
    }

    private void bindGraphics() {
        Spinner preset = findViewById(R.id.gfx_preset);
        Spinner aa = findViewById(R.id.gfx_aa);
        CheckBox shadows = findViewById(R.id.gfx_shadows);
        CheckBox fxaa = findViewById(R.id.gfx_fxaa);
        CheckBox normal = findViewById(R.id.gfx_normal_mapping);
        CheckBox pom = findViewById(R.id.gfx_pom);
        CheckBox water = findViewById(R.id.gfx_water);
        CheckBox vsync = findViewById(R.id.gfx_vsync);
        SeekBar view = findViewById(R.id.gfx_view_distance);
        SeekBar shadowDist = findViewById(R.id.gfx_shadow_distance);
        SeekBar clutter = findViewById(R.id.gfx_clutter);
        TextView viewLabel = findViewById(R.id.gfx_view_distance_value);
        TextView shadowLabel = findViewById(R.id.gfx_shadow_distance_value);
        TextView clutterLabel = findViewById(R.id.gfx_clutter_value);

        ArrayAdapter<String> presetAdapter = new ArrayAdapter<>(
                this, android.R.layout.simple_spinner_dropdown_item,
                new String[] {"Custom", "Low", "Medium", "High", "Ultra"});
        preset.setAdapter(presetAdapter);
        ArrayAdapter<String> aaAdapter = new ArrayAdapter<>(
                this, android.R.layout.simple_spinner_dropdown_item,
                new String[] {"Off", "2x", "4x", "8x"});
        aa.setAdapter(aaAdapter);

        Runnable applyUiFromPrefs = () -> {
            suppressPresetCallback = true;
            preset.setSelection(clamp(prefs.getInt(ObsidianSettings.KEY_PRESET, 2), 0, 4));
            aa.setSelection(clamp(prefs.getInt(ObsidianSettings.KEY_AA, 0), 0, 3));
            shadows.setChecked(prefs.getBoolean(ObsidianSettings.KEY_SHADOWS, true));
            fxaa.setChecked(prefs.getBoolean(ObsidianSettings.KEY_FXAA, false));
            normal.setChecked(prefs.getBoolean(ObsidianSettings.KEY_NORMAL, true));
            pom.setChecked(prefs.getBoolean(ObsidianSettings.KEY_POM, true));
            water.setChecked(prefs.getBoolean(ObsidianSettings.KEY_WATER, true));
            vsync.setChecked(prefs.getBoolean(ObsidianSettings.KEY_VSYNC, true));
            int viewV = Math.round(prefs.getFloat(ObsidianSettings.KEY_VIEW_DISTANCE, 1000f));
            int shadowV = Math.round(prefs.getFloat(ObsidianSettings.KEY_SHADOW_DISTANCE, 150f));
            int clutterV = prefs.getInt(ObsidianSettings.KEY_CLUTTER, 100);
            view.setProgress(clamp(viewV, 200, 3000) - 200);
            shadowDist.setProgress(clamp(shadowV, 50, 500) - 50);
            clutter.setProgress(clamp(clutterV, 0, 200));
            viewLabel.setText(String.valueOf(viewV));
            shadowLabel.setText(String.valueOf(shadowV));
            clutterLabel.setText(String.valueOf(clutterV));
            suppressPresetCallback = false;
        };
        applyUiFromPrefs.run();

        preset.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override public void onItemSelected(AdapterView<?> parent, View view1, int position, long id) {
                if (suppressPresetCallback) return;
                if (position == 0) {
                    prefs.edit().putInt(ObsidianSettings.KEY_PRESET, 0).apply();
                } else {
                    ObsidianSettings.applyPreset(prefs, position);
                    applyUiFromPrefs.run();
                }
            }
            @Override public void onNothingSelected(AdapterView<?> parent) {}
        });
        aa.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override public void onItemSelected(AdapterView<?> parent, View view1, int position, long id) {
                if (suppressPresetCallback) return;
                prefs.edit().putInt(ObsidianSettings.KEY_AA, position)
                        .putInt(ObsidianSettings.KEY_PRESET, 0).apply();
            }
            @Override public void onNothingSelected(AdapterView<?> parent) {}
        });
        shadows.setOnCheckedChangeListener((b, c) -> {
            if (!suppressPresetCallback) prefs.edit().putBoolean(ObsidianSettings.KEY_SHADOWS, c)
                    .putInt(ObsidianSettings.KEY_PRESET, 0).apply();
        });
        fxaa.setOnCheckedChangeListener((b, c) -> {
            if (!suppressPresetCallback) prefs.edit().putBoolean(ObsidianSettings.KEY_FXAA, c)
                    .putInt(ObsidianSettings.KEY_PRESET, 0).apply();
        });
        normal.setOnCheckedChangeListener((b, c) -> {
            if (!suppressPresetCallback) prefs.edit().putBoolean(ObsidianSettings.KEY_NORMAL, c)
                    .putInt(ObsidianSettings.KEY_PRESET, 0).apply();
        });
        pom.setOnCheckedChangeListener((b, c) -> {
            if (!suppressPresetCallback) prefs.edit().putBoolean(ObsidianSettings.KEY_POM, c)
                    .putInt(ObsidianSettings.KEY_PRESET, 0).apply();
        });
        water.setOnCheckedChangeListener((b, c) -> {
            if (!suppressPresetCallback) prefs.edit().putBoolean(ObsidianSettings.KEY_WATER, c)
                    .putInt(ObsidianSettings.KEY_PRESET, 0).apply();
        });
        vsync.setOnCheckedChangeListener((b, c) ->
                prefs.edit().putBoolean(ObsidianSettings.KEY_VSYNC, c).apply());
        view.setOnSeekBarChangeListener(simpleSeek(progress -> {
            int value = progress + 200;
            viewLabel.setText(String.valueOf(value));
            prefs.edit().putFloat(ObsidianSettings.KEY_VIEW_DISTANCE, value)
                    .putInt(ObsidianSettings.KEY_PRESET, 0).apply();
        }));
        shadowDist.setOnSeekBarChangeListener(simpleSeek(progress -> {
            int value = progress + 50;
            shadowLabel.setText(String.valueOf(value));
            prefs.edit().putFloat(ObsidianSettings.KEY_SHADOW_DISTANCE, value)
                    .putInt(ObsidianSettings.KEY_PRESET, 0).apply();
        }));
        clutter.setOnSeekBarChangeListener(simpleSeek(progress -> {
            clutterLabel.setText(String.valueOf(progress));
            prefs.edit().putInt(ObsidianSettings.KEY_CLUTTER, progress)
                    .putInt(ObsidianSettings.KEY_PRESET, 0).apply();
        }));
    }

    private void bindAudio() {
        SeekBar master = findViewById(R.id.audio_master);
        SeekBar music = findViewById(R.id.audio_music);
        TextView masterLabel = findViewById(R.id.audio_master_value);
        TextView musicLabel = findViewById(R.id.audio_music_value);
        CheckBox muteLogin = findViewById(R.id.audio_mute_login);

        int masterV = prefs.getInt(ObsidianSettings.KEY_MASTER, 80);
        int musicV = prefs.getInt(ObsidianSettings.KEY_MUSIC, 30);
        master.setProgress(clamp(masterV, 0, 100));
        music.setProgress(clamp(musicV, 0, 100));
        masterLabel.setText(masterV + "%");
        musicLabel.setText(musicV + "%");
        muteLogin.setChecked(prefs.getBoolean(ObsidianSettings.KEY_MUTE_LOGIN, true));

        master.setOnSeekBarChangeListener(simpleSeek(progress -> {
            masterLabel.setText(progress + "%");
            prefs.edit().putInt(ObsidianSettings.KEY_MASTER, progress).apply();
        }));
        music.setOnSeekBarChangeListener(simpleSeek(progress -> {
            musicLabel.setText(progress + "%");
            prefs.edit().putInt(ObsidianSettings.KEY_MUSIC, progress).apply();
        }));
        muteLogin.setOnCheckedChangeListener((b, checked) ->
                prefs.edit().putBoolean(ObsidianSettings.KEY_MUTE_LOGIN, checked).apply());
    }

    private void bindControls() {
        CheckBox relative = findViewById(R.id.ctrl_relative_mouse);
        CheckBox keepOn = findViewById(R.id.ctrl_keep_screen_on);
        Spinner orientation = findViewById(R.id.ctrl_orientation);

        relative.setChecked(prefs.getBoolean(ObsidianSettings.KEY_RELATIVE_MOUSE, false));
        keepOn.setChecked(prefs.getBoolean(ObsidianSettings.KEY_KEEP_SCREEN_ON, true));

        ArrayAdapter<String> adapter = new ArrayAdapter<>(
                this, android.R.layout.simple_spinner_dropdown_item,
                new String[] {"Landscape (locked)", "Sensor landscape"});
        orientation.setAdapter(adapter);
        String current = prefs.getString(ObsidianSettings.KEY_ORIENTATION, "landscape");
        orientation.setSelection("sensorLandscape".equals(current) ? 1 : 0);

        relative.setOnCheckedChangeListener((b, checked) ->
                prefs.edit().putBoolean(ObsidianSettings.KEY_RELATIVE_MOUSE, checked).apply());
        keepOn.setOnCheckedChangeListener((b, checked) ->
                prefs.edit().putBoolean(ObsidianSettings.KEY_KEEP_SCREEN_ON, checked).apply());
        orientation.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
                prefs.edit().putString(ObsidianSettings.KEY_ORIENTATION,
                        position == 1 ? "sensorLandscape" : "landscape").apply();
            }
            @Override public void onNothingSelected(AdapterView<?> parent) {}
        });
    }

    private void bindPaths() {
        TextView dataDir = findViewById(R.id.paths_data_dir);
        File data = dataDirectory();
        dataDir.setText(data.getAbsolutePath());
        refreshPathsStatus();
    }

    private void bindAbout() {
        TextView version = findViewById(R.id.about_version);
        version.setText(BuildConfig.ENGINE_NAME + " " + BuildConfig.VERSION_NAME
                + " (" + BuildConfig.VERSION_CODE + ")");
    }

    private void refreshHome() {
        TextView device = findViewById(R.id.home_device_line);
        TextView dataLine = findViewById(R.id.home_data_line);
        TextView hint = findViewById(R.id.home_hint_line);
        device.setText(ObsidianSettings.describeDevice());

        File data = dataDirectory();
        boolean ok = hasManifest(data);
        dataLine.setText(ok
                ? "Classic Data found under expansions/classic."
                : "Classic Data missing — push extracted assets before Play.");
        dataLine.setTextColor(getColor(ok ? R.color.obs_ok : R.color.obs_danger));
        hint.setText(ok
                ? "Ready. Fill Sign in if needed, then press Play."
                : "Use scripts/push-game-data.ps1 -Full from your PC.");
    }

    private void refreshPathsStatus() {
        TextView status = findViewById(R.id.paths_manifest_status);
        if (status == null) return;
        File data = dataDirectory();
        boolean ok = hasManifest(data);
        status.setText(ok
                ? "manifest.json detected — client can boot."
                : "No manifest.json yet. Expected under Data/ or Data/expansions/classic/.");
        status.setTextColor(getColor(ok ? R.color.obs_ok : R.color.obs_danger));
    }

    private File dataDirectory() {
        File base = getExternalFilesDir(null);
        if (base == null) base = getFilesDir();
        return new File(base, "Data");
    }

    private boolean hasManifest(File data) {
        return new File(data, "manifest.json").isFile()
                || new File(data, "expansions/classic/manifest.json").isFile();
    }

    private void hideSystemUi() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            WindowInsetsController controller = getWindow().getInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
                controller.setSystemBarsBehavior(
                        WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            //noinspection deprecation
            getWindow().getDecorView().setSystemUiVisibility(
                    View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                            | View.SYSTEM_UI_FLAG_FULLSCREEN
                            | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                            | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                            | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                            | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
        }
    }

    private interface IntConsumer { void accept(int value); }

    private static SeekBar.OnSeekBarChangeListener simpleSeek(IntConsumer onChange) {
        return new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                if (fromUser) onChange.accept(progress);
            }
            @Override public void onStartTrackingTouch(SeekBar seekBar) {}
            @Override public void onStopTrackingTouch(SeekBar seekBar) {}
        };
    }

    private static int clamp(int v, int lo, int hi) {
        return Math.max(lo, Math.min(hi, v));
    }
}
