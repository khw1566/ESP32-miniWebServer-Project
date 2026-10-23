# Designing for the M3 speaker

- The board drives a small mono speaker. Below about 300 Hz it is weak, and
  above about 3.5 kHz it gets harsh. Put melodies between 400 and 2600 Hz.
- Keep one identity per set: reuse the boot melody's intervals in confirm and
  setup so the set sounds related.
- Boot: 0.6 to 1.6 s, ends on the tonic, longest note last.
- Tap: 15 to 40 ms, one note, quieter (gain 40 to 60). It plays on every button,
  so it must not tire the ear.
- Confirm or saved: rising, two notes or one short bright note.
- Error and Wi-Fi failure: falling, lower register, may repeat one note with a
  rest between.
- Wi-Fi setup: up then back down, a "waiting" shape.
- App cues: `base + step * index` for 16 apps, so keep `base + 15 * step`
  under 4000 Hz and the step small enough that neighbours sound alike.
- Square waves are loud: keep gain at or below 60. Bell notes decay, so give
  them 30 to 50 percent more length than a sine note.
- Repeating the same pitch needs a rest (`[0, 40, "sine", 1]`) between notes,
  otherwise the edge fades blur them into one sound.
- Volume is applied later by the user's boot/effect sliders; gain only
  balances notes inside a set.
