package org.endlesssky.esmp;

import org.libsdl.app.SDLActivity;

/**
 * ESMP: SDL2 loads the shared library "main" (libmain.so) and calls
 * SDL_main, which is provided by Endless Sky's main.cpp. The game's
 * resources zip is extracted from the APK assets on first launch.
 */
public class MainActivity extends SDLActivity {
}
