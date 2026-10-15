package org.stormengine.port;

import org.libsdl.app.SDLActivity;

/** Runs the native SDL/Vulkan integration probe, not the game engine. */
public final class ProbeActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {"c++_shared", "SDL2", "storm_probe"};
    }
}
