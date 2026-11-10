# Storm-Engine
Game engine behind [Sea Dogs](https://en.wikipedia.org/wiki/Sea_Dogs_(video_game)), [Pirates of the Caribbean](https://en.wikipedia.org/wiki/Pirates_of_the_Caribbean_(video_game)) and [Age of Pirates](https://en.wikipedia.org/wiki/Age_of_Pirates_2:_City_of_Abandoned_Ships) games.

[![Join the chat at https://discord.gg/jmwETPGFRe](https://img.shields.io/discord/353218868896071692?color=%237289DA&label=storm-devs&logo=discord&logoColor=white)](https://discord.gg/jmwETPGFRe) 

 * [GitHub Discussions](https://github.com/storm-devs/storm-engine/discussions)
 * [Discord Chat](https://discord.gg/jmwETPGFRe)

## Supported games
- [Sea Dogs: To Each His Own](https://github.com/storm-devs/sd-teho-public)
- [Sea Dogs: City of Abandoned Ships](https://store.steampowered.com/app/937940/Sea_Dogs_City_of_Abandoned_Ships/) (work in progress)
- [Pirates of the Caribbean: New Horizons](https://www.piratesahoy.net/wiki/new-horizons/) (work in progress)

<p align="center">
<img src="https://steamuserimages-a.akamaihd.net/ugc/879748394074455443/FD04CEA2434D8DACAD4886AF6A5ADAA54CDE42AA/">
</p>

## Building the project
This port builds for Android (ARM64) and, for development, for the Linux host. The renderer is
[bgfx](https://github.com/bkaradzic/bgfx) on OpenGL ES 3.0; its pinned dependencies live in
`src/libs/renderer_bgfx` (see its README) and their licences in `third_party/licenses`.

- Android: `bash tools/android/ci_dependencies.sh`, then `bash tools/android/ci_engine.sh`
  (see `android/README.md` and `tools/android/README.md`).
- Linux host: configure with `-DSTORM_HOST_SOURCES=<prepared sources>` and
  `-DSTORM_AUDIO_SOURCES=<prepared audio sources>` (see `cmake/host.cmake`).

You will also need assets from one of the supported games.

## Roadmap
Since our development team is small, we want to reduce the amount of code we have to maintain.
For this reason, we decided to rely on the C++ standard library or third-party libraries if possible.

Some things that we are going to do:
- Replace custom input handling code with a third-party library (see the [discussion](https://github.com/storm-devs/storm-engine/discussions/19)).
- Replace custom data structures with C++ standard types.
- Replace a custom scripting language with Lua.
- Replace ini config files with a standard format (see the [discussion](https://github.com/storm-devs/storm-engine/discussions/26)).
- Replace custom binary asset formats with standard ones.
- Update code using the latest C++ standard

## Contributing
If you'd like to get involved, please check [CONTRIBUTING.md](https://github.com/storm-devs/storm-engine/blob/develop/CONTRIBUTING.md).

## License
[GPL-3.0 License](https://choosealicense.com/licenses/gpl-3.0/)
