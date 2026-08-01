# VocalForge — 8 Count Music

An all-in-one vocal chain VST3 plugin: cleanup (gate, EQ, de-esser, compressor, saturation, parallel compression) plus a character engine (Deep, Robot, Harmony, EDM, Dubstep) and reverb/delay — in one window.

Built with JUCE + Signalsmith Stretch. Validated with pluginval at maximum strictness.

---

## Getting the plugin onto your computer (no coding tools needed)

The code has to be compiled once for your machine (Windows or Mac). The easiest way is to let GitHub do it for you, for free:

1. **Create a free GitHub account** at github.com (skip if you have one).
2. **Create a new repository**: click the "+" in the top-right → "New repository". Name it `vocalforge`, keep it **Public** (private works too), and click "Create repository". Don't add a README when it asks.
3. **Upload this project**: on the new repo page, click the link "**uploading an existing file**". Drag in **everything inside this folder** — including the `.github` folder (it may be hidden on Mac: press `Cmd+Shift+.` in Finder to show hidden files). Click "Commit changes".
   - *Easier alternative:* install the free **GitHub Desktop** app, "Add local repository" pointing at this folder, then Publish. It never misses hidden folders.
4. **Wait ~10 minutes**: go to the **Actions** tab of your repo. You'll see "Build VocalForge" running. When it turns green, click it.
   - If the Actions tab is empty, the `.github` folder didn't upload — use the GitHub Desktop route.
5. **Download your plugin**: at the bottom of the finished run, under "Artifacts", download `VocalForge-Windows-VST3` and/or `VocalForge-macOS-VST3` and unzip it.

## Installing into Reaper

- **Windows**: copy the whole `VocalForge.vst3` folder into `C:\Program Files\Common Files\VST3\`
- **Mac**: copy `VocalForge.vst3` into `/Library/Audio/Plug-Ins/VST3/` (or `~/Library/Audio/Plug-Ins/VST3/`). Because it isn't Apple-notarized, if the Mac blocks it, open Terminal and run:
  `sudo xattr -rd com.apple.quarantine "/Library/Audio/Plug-Ins/VST3/VocalForge.vst3"`

Then in Reaper: Options → Preferences → Plug-ins → VST → **Re-scan**. Insert "VocalForge" on your vocal track.

## Presets

The menu at the top-right holds factory presets (Clean & Tight, Radio Ready, Trailer Voice, Monster, Cyborg, Broken Android, Choir Stack, Psy Alien, Festival Lead, Wub Machine, Growl Bass) plus your own: hit **SAVE** or "Save current as..." and it's stored in `Documents/VocalForge Presets` and appears in the menu under USER.

The top strip shows the live output spectrum with your EQ curve drawn over it, and stereo output meters with peak hold.

## Tune + MIDI (v1.2)

- **TUNE** — autotune. Pick KEY and SCALE, then SPEED sets the style: high = instant hard-tune robot snap, low = gentle correction. AMOUNT scales how far it pulls.
- **MIDI FOLLOW** — send MIDI notes to the VocalForge track (route a keyboard or MIDI item to it in Reaper) and the tuner snaps the vocal to exactly the notes you hold; in Harmony mode the harmony voices follow your held chord instead of fixed intervals.
- **MIDI OUT** — the plugin detects the vocal's pitch and emits it as MIDI notes. In Reaper, add a receive on a synth track from the VocalForge track (MIDI), and the synth plays the vocal melody — instant doubling layers.
- The PITCH display shows detected note → target note in real time.

## The controls

**Signal flow:** Input → Gate → EQ → De-esser → Compressor → Saturation → Parallel comp → Character → Reverb/Delay → Output.

- **INPUT / GATE** — level in, and a threshold that mutes room noise between phrases (fully off at -80).
- **EQ** — LOW CUT removes rumble; CUT FREQ + CUT DEPTH + CUT Q is your surgical "bad frequency" notch (sweep FREQ with DEPTH down to find the ugly spot); HARSH tames 3.5 kHz edge; AIR adds top-end shine.
- **DYNAMICS** — DE-ESS softens S sounds; THRESH/RATIO/ATTACK/RELEASE/MAKEUP is the main compressor.
- **TONE** — SATURATE is warm drive; PARALLEL blends in a smashed copy for thickness (classic NY compression).
- **CHARACTER** — pick a mode, then: AMOUNT = dry/wet morph, TUNE and COLOR change meaning per mode:
  - **Deep** — movie-trailer voice. TUNE fine-tunes the drop, COLOR darkens the formant.
  - **Robot** — ring-mod + bitcrush. TUNE moves the metallic pitch, COLOR adds crush.
  - **Harmony** — two harmony voices (3rd + 5th) under your lead. TUNE shifts the interval set, COLOR sets harmony level.
  - **EDM** — formant lift + OTT-style multiband squash + chorus width. COLOR = brighter/harder.
  - **Dubstep** — tempo-synced resonant wobble. COLOR picks the rate (whole note → 1/16), TUNE moves the growl center. Syncs to your project BPM (defaults to 150).
- **SPACE** — REVERB + SIZE, and a tempo-synced DELAY (1/8, dotted 1/8, or 1/4).

The pitch-based modes add a small latency; Reaper compensates automatically.

## Vibe-coding your changes

This is your source code — change anything. The workflow:

1. Open this folder in Claude Code (or paste a file into Claude) and describe what you hear: "the Deep mode is too muddy, brighten it" or "add a fourth harmony voice an octave up".
2. Edit → commit/upload to GitHub → the Actions build gives you a fresh plugin in ~10 minutes.

Where things live:

- `src/Params.h` — every knob's range and default
- `src/dsp/CleanupChain.h` — gate, EQ, de-esser, compressor, saturation, parallel
- `src/dsp/CharacterEngine.h` — all five character modes
- `src/dsp/Space.h` — reverb + delay
- `src/PluginEditor.cpp` — the look (colors are at the top of `PluginEditor.h`)

## License note

JUCE is used under its free GPLv3 option, which is fine for personal use and open-source projects. If you ever want to sell VocalForge as a closed-source product, you'd need a paid JUCE license. Signalsmith Stretch is MIT-licensed (free for anything).
