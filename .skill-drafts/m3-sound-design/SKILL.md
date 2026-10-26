---
name: m3-sound-design
description: Design, preview and ship boot melodies and UI effect sounds for the Mini WebServer M3 board (ESP32-P4, small mono speaker). Use when adding or changing sound sets, boot sounds, tap/confirm/error cues or the Settings sound set list, or when the user wants new system sounds without spending Flash on WAV files.
---

# M3 sound design

The M3 firmware synthesizes system sounds from note tables, so a whole sound set
costs about a hundred bytes of Flash. Sets live in
`mini-WEB (M3-ESP-IDF_ver)/tools/sound_packs.json`; the device plays them through
`note()` in `main/m3_feedback.c`. Spoken guidance WAVs on the SD card are a
separate system (`tools/generate_system_sounds.py`) and are not touched here.

Project rules still apply: do not compile or flash firmware; the user builds.

## Workflow

1. Read `references/small-speaker.md` before choosing notes.
2. Edit `tools/sound_packs.json`. Each set has `boot`, `tap`, `confirm`, `error`,
   `setup` note lists and an `app` rule. A note is `[Hz, ms, wave, gain]`;
   `Hz = 0` is a rest. Waves: `sine`, `triangle`, `square`, `bell`.
3. Render and measure previews (writes WAVs, changes nothing in the repo):

   ```powershell
   python <skill-directory>/scripts/sound_lab.py --project "<firmware-directory>" --out "<scratch>/sound_previews"
   ```

   Fix every `WARN` line or explain why it stays. Send the `<id>_demo.wav`
   files to the user to audition when they asked to hear the sounds.
4. Generate the firmware table and run the host checks:

   ```powershell
   python tools/generate_sound_packs.py
   node tools/test_sound_packs.cjs
   node tools/test_system_feedback.cjs
   ```

5. Adding or removing a set also changes `M3_SOUND_PACK_COUNT` in
   `main/include/m3_feedback.h`, the `T_PACK_*` names in
   `main/include/m3_i18n.h`, `main/m3_i18n.c`, `translations/ja.json`
   (then `python tools/generate_japanese.py`), and the name list in
   `main/m3_sound_ui.inc`. The saved choice (`m3_device/sound_pack`) falls back
   to set 0 when it is out of range, so keep `classic` first.
6. Update `docs/SYSTEM_SOUNDS.md` when sets change.

## Changing the synthesis

`scripts/sound_lab.py` mirrors `note()` exactly. If you change the envelope,
waves or amplitude in `m3_feedback.c`, make the same change in the script so
previews stay truthful, and keep the work bounded: one 240-frame stereo block on
the audio task stack, no allocation, no extra task.
