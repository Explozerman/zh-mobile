package io.github.explozerman.zhmobile;

import android.os.Bundle;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;

/**
 * Hosts the game engine (libmain.so). Launched by {@link LauncherActivity} in a
 * separate ":game" process with the launcher's settings as SDL_main arguments.
 */
public class GameActivity extends SDLActivity {
    public static final String EXTRA_ARGS = "io.github.explozerman.zhmobile.ARGS";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // An RTS has long stretches without touch input; don't let the screen sleep.
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    }

    @Override
    protected String[] getLibraries() {
        // Dependencies (libc++_shared, OpenAL, ...) are resolved by the dynamic
        // linker; libdxvk_d3d8.so is dlopen()ed by the engine at renderer start.
        return new String[] { "SDL3", "main" };
    }

    @Override
    protected String[] getArguments() {
        String[] args = getIntent() != null ? getIntent().getStringArrayExtra(EXTRA_ARGS) : null;
        return args != null ? args : new String[0];
    }
}
