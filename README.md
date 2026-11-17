# Octaline

An 8-band modulation delay plugin (VST3 and AU) for macOS, modelled on the
behaviour of a classic eight-band rack/stomp delay. Each band has its own time,
loop filters, feedback, tap, modulation, pan, level and phase, and bands can
run in parallel, in series, or chained into one long delay of up to 5.9 s.

* 87 factory presets, plus your own presets saved from inside the plugin
* Host tempo sync and tap tempo per band
* Copy and paste settings between bands
* Every control can be automated from the DAW

See [docs/behaviour.md](docs/behaviour.md) for exactly how each control works.

## Install a ready-made build

Every push to `main` builds the plugin for Mac (Apple Silicon and Intel).

1. Open the repository's **Actions** tab, click the latest green **Build**
   run, and download **Octaline-mac** at the bottom of the page. Tagged
   versions (`v0.1.0` and so on) also appear under **Releases**.
2. Unzip it and follow `README.txt` inside (one Terminal command).

## Build it yourself on a Mac

You need Xcode (free, from the App Store) and CMake (`brew install cmake`, or
the installer from cmake.org).

```sh
git clone https://github.com/msintros/octaline.git
cd octaline
cmake -S . -B build -G Xcode -DOCTALINE_INSTALL=ON
cmake --build build --config Release
```

With `OCTALINE_INSTALL=ON` the build copies the plugin into
`~/Library/Audio/Plug-Ins/VST3` and `~/Library/Audio/Plug-Ins/Components`.
The first configure downloads JUCE, so it takes a few minutes.

To run the tests: `ctest --test-dir build -C Release`.

## Code layout

| Path | What it is |
|---|---|
| `src/dsp/Engine.*` | The delay engine, plain C++ with no framework, unit tested |
| `src/plugin/` | JUCE plugin: parameters, presets, editor |
| `tests/EngineTests.cpp` | Timing and level checks against the manual's examples |
| `tests/PluginHarness.cpp` | Loads every preset, round-trips saved state, opens the editor |
| `tools/extract_patch_list.py` | Regenerates the factory presets from the patch list PDF |

## Licence note

Octaline uses [JUCE](https://juce.com) 8, which is free for personal use
under its Starter licence (or AGPLv3). This is an independent project, not
affiliated with or endorsed by any hardware manufacturer.
