# whysho SY1

A straightforward virtual-analog synthesizer — the first instrument in the whysho suite.
Everything you need to build a sound sits on one page; a ZENOLOGY-style browser gets you to a
sound fast, and banks are plain files you can share.
Built on [JUCE](https://juce.com) (C++), shipping as **VST3** (Windows + macOS), **AU** (macOS) and Standalone.

![status](https://img.shields.io/badge/stage-alpha-orange) ![formats](https://img.shields.io/badge/formats-VST3%20%7C%20AU-blue)

---

## Features

**Sound**
- **3 oscillators**: Saw, Square, Triangle, Sine, SuperSaw, Noise — band-limited, each with octave / semi / fine, level, pan and a **Shape** knob (pulse width, supersaw detune, or wavefold, depending on the wave)
- Sub oscillator, noise, and **FM** (osc 2 → osc 1)
- **Filter**: LP 24, LP 12, BP, HP 12, HP 24, Notch — with drive, envelope amount and key tracking
- Amp and filter **ADSR**, plus a **pitch envelope** (the 808 knock)
- **2 LFOs**, free or tempo-synced, and a **4-slot mod matrix** (LFOs, envelopes, velocity, mod wheel, key track, random → pitch, cutoff, shape, level, pan, …)
- **Poly / Mono / Legato** with **glide** (sliding 808s), **unison** up to 8 voices with detune and spread
- **Effects**: Drive (Soft, Hard, Tube, Fold, Crush), Chorus, Delay (tempo-synced, ping-pong), Reverb
- 16 voices, sample-accurate MIDI, pitch bend, sustain pedal, output safety limiter

**Presets & banks**
- **Browser** with banks, categories (808, Bass, Lead, Pluck, Keys, Piano, Bell, Synth, Pad, Pulse, Strings, Brass, FX), favourites and search
- **Factory bank**: 62 sounds, mostly trap
- **Your own sounds**: save into user banks; create, rename, delete banks
- **Share**: a bank is one `.sy1bank` file (JSON). Export it, send it, import it — or drop it on the plugin. Single sounds travel as `.sy1preset`. Format: [`docs/BANKS.md`](docs/BANKS.md)

**Interface**
- One-page layout, signal flow left to right; draggable envelopes, live wave / filter / LFO displays, scope
- On-screen keyboard, playable from the computer keyboard
- **Themes**, shared with the rest of the suite: **Paper** (default), **Midnight**, **Graphite** as in EQ1, plus **Riso**, **Ultraviolet**, **Acid**, **Blood**
- Resizable UI

**Licensing** — see [`docs/LICENSING.md`](docs/LICENSING.md)
- **Sign in with your whysho.com account** (email + password, or in the browser for Google / Apple accounts); the plugin unlocks while the account owns SY1, with an offline grace period
- License **key** activation → machine-bound, **Ed25519-signed** token verified offline (same scheme as EQ1)
- 14-day local trial; layered anti-tamper

---

## Repository layout

```
CMakeLists.txt        Root build (fetches JUCE + Monocypher)
src/Parameters.*      The parameter table — single source of truth for ids, ranges, defaults
src/dsp/              Synth engine: oscillators, voices, filter, modulation, effects
src/presets/          Bank file format + factory bank
src/gui/              Editor components, themes, preset browser, license dialog
src/licensing/        Account sign-in, license keys, trial, anti-tamper
packaging/            Inno Setup (.exe) + macOS .pkg installer configs
.github/workflows/    CI: build + package + release on Win & macOS
tools/                Dev only: headless audio render / self-test, offscreen GUI snapshots
docs/                 Licensing + bank format
```

## Building

**Requirements:** CMake ≥ 3.22, a C++17 compiler. First configure downloads JUCE 8 + Monocypher.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Artefacts land in `build/WhyshoSY1_artefacts/Release/` (`VST3/`, `AU/`, `Standalone/`).

Installers are built by CI; locally see [`packaging/README.md`](packaging/README.md).

## Dev tools

```bash
cmake -B build -DWHYSHO_BUILD_RENDER=ON -DWHYSHO_BUILD_SNAPSHOT=ON
cmake --build build

# engine self-test (tuning, aliasing, stability, fuzzing, voice modes …)
build/WhyshoSY1_Render_artefacts/Release/WhyshoSY1_Render --selftest

# render a preset to WAV with stats
build/WhyshoSY1_Render_artefacts/Release/WhyshoSY1_Render --preset "Factory/Long Sub" --notes 36 --out sub.wav

# UI screenshots in every theme (Linux: run under xvfb-run)
build/WhyshoSY1_Snapshot_artefacts/Release/WhyshoSY1_Snapshot shots/
```

## Adding things

- **A parameter**: one enum entry in `src/Parameters.h` + one row in the table in `src/Parameters.cpp`. The host layout, the engine's value array and the preset format all follow from it. Never reorder or remove shipped ids.
- **A theme**: append one entry to the catalogue in `src/gui/Theme.h`.
- **A factory preset**: add it to `src/presets/FactoryBank.cpp`.

## Security / "non-crackable" — honest note

As with EQ1: no locally-run plugin can be made truly uncrackable. SY1 uses the same layered
deterrent — signed machine-bound licenses, a sealed account cache, obfuscated strings, a
redundant audio-path gate — to make cracking expensive and casual sharing impractical.
See [`docs/LICENSING.md`](docs/LICENSING.md).
