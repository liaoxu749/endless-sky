package org.endlesssky.esmp;

import org.libsdl.app.SDLActivity;

/**
 * ESMP: SDL2 loads the shared library "main" (libmain.so) and calls
 * SDL_main, which is provided by Endless Sky's main.cpp. The game's
 * resources zip is extracted from the APK assets on first launch.
 */
public class MainActivity extends SDLActivity {
	// SDLActivity loads "SDL2" plus companion libs by default, but our build
	// statically links SDL2 into libmain.so, so only that library exists.
	@Override
	protected String[] getLibraries() {
		return new String[] {
			"main"
		};
	}
}

