# Polisher User Manual

Version 0.13.0 · October 2026

Live, shareable copy: https://claude.ai/code/artifact/3bf99dd7-1111-4be3-884d-d787ae49506c (this file is its source; keep both in step).

## What Polisher is

Polisher is a mastering plug-in: it takes your finished stereo mix and makes it louder, clearer and ready to release. It holds eleven modules in one window, and **Auto-Master** can set all of them for you in about 12 seconds.

This manual covers version 0.13.0. Every control is explained in plain words, with the number ranges the plug-in really uses.

**The 30-second version**

1. Put Polisher in the **last insert slot of Stereo Out**.
2. Turn off any other limiter, clipper or mastering chain on Stereo Out.
3. Pick your genre, an intensity and a target at the top.
4. Play the loudest part of the song (the hook or the drop) and press **Auto-Master**.
5. Turn on **MATCH** and flip **A / B** to check the result at equal loudness.
6. Read the **Learn** panel on the right: it explains every choice and tells you if anything needs fixing.

## Getting started in Cubase

Polisher goes on **Stereo Out**, the channel every track flows into, in the **last insert slot**. On a single track it can't master anything, and Auto-Master will refuse and tell you so.

### Install

1. Download the Windows build (Polisher-Windows) and unzip it.
2. Copy **Polisher.vst3** into `C:\Program Files\Common Files\VST3`.
3. Start Cubase. If Polisher doesn't show up, open **Studio > VST Plug-in Manager** and click **Update Plug-in Information**.

Polisher's screen runs on the **Microsoft Edge WebView2 Runtime**. Windows 11 has it built in and Windows 10 gets it with Microsoft Edge, so you rarely need to do anything. If Polisher opens a small screen that says **BASIC VIEW**, the runtime is missing: click **GET WEBVIEW2**, install the free "Evergreen" runtime from Microsoft, then close and reopen Polisher. Your settings are kept, and the whole chain keeps processing audio in the basic view.

### Load it on Stereo Out

1. Press **F3** to open the MixConsole.
2. Find **Stereo Out** on the far right.
3. Click the **last insert slot** and choose Polisher.
4. Bypass every other plug-in on Stereo Out that changes loudness or tone (limiters, clippers, compressors, mastering EQs). Polisher replaces that chain; stacking two chains makes the input far too hot.

### Set your mix level

Leave headroom going in. A good input is about **−17 LUFS integrated with peaks around −1 dBTP**. If your mix is hotter, pull it down (for example, lower the Stereo Out fader before Polisher, or turn down the mix bus by about 6 dB).

### Your first master

1. Click the genre name at the top and choose yours (Trap / Hip-Hop, R&B, Pop, EDM, Rock, Acoustic).
2. Choose **Intensity**: Gentle, Balanced or Punchy. Balanced is the default.
3. Choose **Target**: Stream −14, Loud −9 or Club −7. Loud −9 is the default.
4. Play the loudest part of your song and click **Auto-Master**. Keep it playing until it finishes.
5. Turn on **MATCH**, flip **A / B** and listen.
6. Read Learn. Red boxes need a decision; blue boxes are tips.

### Using the knobs

- Drag a knob **up or down**. Hold **Shift** for fine moves.
- **Double-click** a knob to reset it.
- The **mouse wheel** works too.

### Exporting

Export as usual with **File > Export > Audio Mixdown**. MATCH switches itself off during export, so your file is always the real master. Polisher reports its delay to Cubase, which lines everything up for you.

## The signal chain

The order is fixed, from repair to loudness: fix the mix's dynamics first, then tone, then low end, compression and width, then colour, and finally peaks and loudness.

```
01 Unlimiter -> 02 Tonal EQ -> [03 Stem EQ / 04 Tone Steady / 05 Resonance Tamer] -> 06 Bass Control
-> 07 Dynamics -> 08 Stereo Imager -> 09 Saturation -> Drive -> 10 Soft Clipper -> 11 Maximizer -> your master
(03-05 share one stage, ~40 ms; MATCH gain sits at the very end, for listening only)
```

The Maximizer's **Drive** sits just before the Soft Clipper, so the clipper and limiter share the peak work. MATCH only changes what you hear while comparing; it is off during export.

## Auto-Master

Auto-Master listens to 12 seconds of your song, then sets every module except Stem EQ and explains each choice. It skips silence while it listens, and needs at least 3 seconds of music.

### The three choices

| Choice | Options | What it changes |
| --- | --- | --- |
| Genre | Trap / Hip-Hop, R&B, Pop, EDM, Rock, Acoustic | The tone balance it aims for, the saturation style and the usual vocal level |
| Intensity | Gentle, Balanced, Punchy | How far it may move the EQ and how hard it may squash peaks |
| Target | Stream −14, Loud −9, Club −7 | The loudness it aims for, in LUFS |

| Intensity | Largest EQ move | Peak control allowed | Character |
| --- | --- | --- | --- |
| Gentle | 1.5 dB | 2 dB | Most transparent; no Soft Clipper, no kick Punch |
| Balanced | 3 dB | 3.5 dB | The everyday choice |
| Punchy | 4 dB | 5.5 dB | Louder and harder; more Punch and clipping |

### How to run it

1. Set Genre, Intensity and Target.
2. Play the **loudest part** of the song (hook, chorus or drop).
3. Click **Auto-Master** and let it play until it finishes.
4. Read the report in Learn (**WHY IT SOUNDS LIKE THIS**).
5. Don't like it? Click the **undo arrow** to put every control back as it was.

### What to expect

- It may stop **short of the target** on purpose. If reaching it would crush the drums past the intensity's limit, it keeps the punch and tells you. On a trap mix at about −17 LUFS in, Loud −9 typically lands near −9.7 LUFS.
- A module marked **Checked · kept** was measured and needed no change.
- **Stem EQ** is never changed. If your vocal sits unusually low or loud for the genre, it suggests a Vocals move (up to ±3 dB) with an **APPLY** button.
- If you change a module after Auto-Master, Learn notes that its reasons no longer describe what you hear.
- If the audio doesn't sound like a full mix (for example, Polisher is on one track), Auto-Master changes nothing and tells you to move it to Stereo Out.

Every warning in the report says **Do I need to fix it?** and gives numbered steps. Auto-Master is a starting point: your ears and MATCH make the final call.

## Every module and its controls

Click a module in the **Signal Chain** (left) to open it. Each module has its own on/off switch (the button at the top right of the module panel, for example **UNLIMITER ON**), and every module is an exact pass-through at its neutral settings, so a module you don't use changes nothing. The Maximizer's button is different: **BYPASS** there bypasses the whole chain, the same as **A**.

### 01 Unlimiter

Puts back the punch a limiter or clipper squashed out of a beat, loop or mix. It lifts the start of each hit; the level between hits stays put. It can't rebuild a waveform a clipper flattened, but it restores the contrast between hits and the rest.

| Control | Range (default) | What it does |
| --- | --- | --- |
| Amount | 0–100 % (0) | How much punch is put back. Start at 75 %. |
| Length | 5–50 ms (15) | How long each lift lasts. Shorter if vocals or pads jump on each hit. |
| Focus | Full, Kick, Snap | Which hits it listens for: every hit, kick and 808 (below 150 Hz), or snares and hats (above 1 kHz). |
| Squash meter | open … wall | How flattened the incoming audio is. 12.5 dB or more = open (it rests); 8.5 dB or less = brickwalled (full strength). |
| Restoring meter | 0–12 dB | The lift right now. |

On an open, unmastered mix the Unlimiter rests on purpose, even with Amount up. Use it on beats or loops that arrive already loud and flat.

### 02 Tonal EQ

Shapes the overall tone in seven bands. Turn a band's knob, or drag its numbered point on the spectrum: sideways moves the frequency, up/down sets the gain. Double-click a point to reset it. Each band goes **±9 dB**; master moves of 1–3 dB go a long way.

| Band | Default | Can move | Boost | Cut |
| --- | --- | --- | --- | --- |
| Low End (shelf) | 45 Hz | 20–120 Hz | Bigger sub, 808 tail | Less rumble, more headroom |
| Bass | 100 Hz | 50–250 Hz | Fuller kick and 808 body | Tighter low end |
| Low Mid | 350 Hz | 150–800 Hz | Warmth (too much = mud) | Clears mud and boxiness |
| Mid | 1 kHz | 500 Hz–2.5 kHz | Vocals and snare forward | Scooped, hyped sound |
| Upper Mid | 2.5 kHz | 1.2–5 kHz | Vocal clarity, snare crack | Smooths harshness |
| Presence | 5 kHz | 2.5–10 kHz | Edge on vocals and hats | Softens 's' sounds |
| Air (shelf) | 12 kHz | 6–20 kHz | Sheen on top | Darker, tames fizzy hats |

On the spectrum, the white line is your mix, the blue curve is the EQ change and the red line is the master after the whole chain.

### 03 Stem EQ

Turns the **Vocals, Drums, Bass** (808 included) or **Other** (synths, keys, guitars, pads, effects) up or down inside your finished stereo mix, like moving one fader back in the session. A small AI model works out, moment by moment and frequency by frequency, how much of the sound is each part.

| Control | Range (default) | What it does |
| --- | --- | --- |
| Vocals / Drums / Bass / Other | ±12 dB (0) | That part's level. 1–3 dB is usually all you need; up to about ±6 dB stays clean. |
| Model Hears | meter | How the model divides your music right now. |

Trade-offs: it adds about 40 ms of delay (Cubase compensates it). Where two parts share the same notes, a move nudges both a little; Other moves touch the drums slightly.

### 04 Tone Steady

Keeps the tone consistent through the song. When a section drifts muddier, harsher or thinner than the song's own balance, it nudges it back. It doesn't change your overall sound; that's the Tonal EQ's job.

| Control | Range (default) | What it does |
| --- | --- | --- |
| Amount | 0–100 % (0) | How much drift is corrected. 40 % is a good start. |
| Range | 1–6 dB (3) | Largest cut; boosts go up to half of it. |
| Speed | Slow, Medium, Fast (Medium) | Reacts to sections (1 s), phrases (0.4 s) or notes (0.15 s). |
| Steadying meter | 0–6 dB | The biggest move right now. |

It learns your song's balance over about 10 seconds, leaves quiet breakdowns alone, and is gentle on the top end so busy hi-hats keep their sparkle. If the drop feels smaller, use Slow or lower Amount.

### 05 Resonance Tamer

Finds ringing tones that stick out (boxiness, whistly vocal tones, harsh hats) and cuts only those, only while they ring.

| Control | Range (default) | What it does |
| --- | --- | --- |
| Amount | 0–100 % (0) | How deep the cuts go. Start at 50 %. |
| Sensitivity | 0–100 % (50) | Higher catches smaller peaks. |
| Release | 20–300 ms (80) | How fast a cut lets go. |
| Focus | Full (80 Hz–16 kHz), Boxiness (150–800 Hz), Harshness (1.5–8 kHz) | Where it looks. |
| Cutting meter | 0 to −12 dB | The deepest cut right now. |

### 06 Bass Control

Keeps the low end solid on every system.

| Control | Range (default) | What it does |
| --- | --- | --- |
| Mono Below | off–250 Hz (off) | Makes the bass below this mono. About 100 Hz is safe and helps clubs and phones. |
| Sub Tame | 0–100 % (0) | Turns down 808 notes that jump above the song's usual low end (up to 12 dB, around 120 Hz). |
| Punch | 0–100 % (0) | Adds up to 4 dB of thump (around 80 Hz) to the start of each kick, without raising the sub. |
| Taming meter | 0 to −12 dB | How much Sub Tame is cutting. |
| Bass Width meter | mono … wide | How stereo your mix is below 150 Hz. |

### 07 Dynamics

Four-band compression. Click a band button (LOW, LO-MID, HI-MID, HIGH) to edit it; each button shows that band's gain reduction.

| Band | Default split | Holds |
| --- | --- | --- |
| Low | below 120 Hz | Kick and 808 |
| Low Mid | 120 Hz–1 kHz | Body, snare, vocal warmth |
| High Mid | 1–6 kHz | Vocal presence, snare crack |
| High | above 6 kHz | Hats, cymbals, air, sibilance |

| Control | Range (default) | What it does |
| --- | --- | --- |
| Split | 40–300 Hz, 300 Hz–3 kHz, 3–12 kHz | Where the band starts or ends. |
| Threshold | −48 to 0 dB (−20) | Where compression starts. |
| Ratio | 1:1–10:1 (1:1 = off) | How firmly the band is held. 2:1 is plenty. |
| Attack | 0.5–100 ms (30 / 20 / 10 / 5) | Slow lets drum hits through; fast catches peaks. |
| Release | 20–1000 ms (200 / 150 / 120 / 100) | How fast it lets go. |
| Gain | ±12 dB (0) | The band's level after compression. |
| Adaptive release | on | Lets go quickly after short hits, slower on long notes. |

How to set a band: raise Ratio to 2:1, then lower Threshold until the band button shows 1–3 dB on the loud hits.

### 08 Stereo Imager

Sets the width of lows, mids and highs. Only the stereo (side) part changes; the centre (vocal, kick, 808) is never touched.

| Control | Range (default) | What it does |
| --- | --- | --- |
| Lows | below 250 Hz, 0–200 % (100) | Keep at 100 %. Wide bass smears and costs headroom. |
| Mids | 250 Hz–5 kHz, 0–200 % (100) | Small moves only. |
| Highs | above 5 kHz, 0–200 % (100) | Hats, reverbs, ad-libs. Try 110–115 %. |
| Correlation meter | −1 to +1 | Mono safety. +1 = mono, 0 = wide, below 0 = out of phase. Keep it above +0.3. |

### 09 Saturation

Adds harmonics so bass translates on small speakers and the master feels denser. Quiet parts pass untouched; the level stays put as you add Drive.

| Control | Range (default) | What it does |
| --- | --- | --- |
| Mode | Tape, Tube, Clip (Tape) | Tape = smooth and warm. Tube = rich, fuller bass. Clip = firm and bright. |
| Drive | 0–18 dB (0) | How many harmonics. 0 = off. |
| Mix | 0–100 % (100) | Blend with the clean sound. |
| Harmonics meter | −60 to −10 dB | What was added. Aim for about −25 to −35 dB. |

### 10 Soft Clipper

Shaves the tallest drum peaks before the Maximizer, so the limiter works less. This is how loud modern masters stay punchy. It's off until you turn it on.

| Control | Range (default) | What it does |
| --- | --- | --- |
| Clip Level | −9 to +3 dB (0) | Measured from the Maximizer ceiling. Lower = more peaks shaved. |
| Softness | hard–soft, 0–100 % (50) | A rounder knee sounds like punch, not distortion. |
| Oversampling | 1x, 2x, 4x, 8x (4x) | Stops harsh aliasing. 4x is the mastering default; 8x is cleanest. |
| Clipping meter | 0 to −6 dB | Peak reduction. Aim for 1–3 dB on the drums. |

### 11 Maximizer

The final true-peak limiter. It makes the master loud and guarantees nothing goes over the ceiling.

| Control | Range (default) | What it does |
| --- | --- | --- |
| Drive | 0–24 dB (0) | Pushes the mix into the limiter. More Drive = louder. |
| Ceiling | −3 to 0 dBTP (−1) | The highest peak allowed, between samples too. −1 keeps streaming MP3/AAC from clipping. |
| Release | 10–1000 ms (120) | How fast the limiter lets go. Shorter = louder but can distort bass. |
| Release Mode | Adaptive (default) or Fixed | Adaptive recovers fast after drum hits and slowly on 808s and pads, so it doesn't pump. |
| GR | meter | Gain reduction. Under about 4 dB is comfortable. |

Set Drive while watching **Short-term** in the footer. If Learn says **Diminishing returns**, the extra Drive only adds squashing, not loudness: turn it down.

## MATCH and A / B

Louder always sounds better, so compare at equal loudness: turn on **MATCH**, then flip **A / B**.

- **A** plays your original mix (Polisher bypassed). **B** plays the master.
- With MATCH on, play **B for about 10 seconds** first. Polisher learns how much louder the master is, then turns the louder side **down** to match. It never turns anything up, so B can't go past its ceiling.
- If your original mix peaks above 0 dBFS, both sides are turned down together so A doesn't clip your audio interface and sound worse than it is.
- Now judge tone and punch, not volume. Ask: is B clearer, fuller, punchier? Did the kick or vocal lose anything?
- The meters always show the **real master**, not the matched level.
- MATCH switches itself off when you close Polisher and during export.

On headphones, compare at a low, comfortable volume and take breaks. Ears tire fast, and tired ears like everything brighter.

## Learn and Quick Help

The **Learn** panel on the right explains, live, what the selected module is doing to your track. Click **LEARN ON** at the top to show or hide it.

- **WHAT'S HAPPENING**: what you're hearing right now.
- **WHAT EACH CONTROL IS DOING**: each control's value and its effect on your song.
- **Red boxes** need a decision. Each one says **Do I need to fix it?** and gives numbered **How** steps.
- **Blue boxes** are tips, or warnings that don't need fixing. Nothing blue is a problem.
- After Auto-Master, the top of each module's Learn shows the reasons for its settings, and **WHY IT SOUNDS LIKE THIS** holds the whole report.

**Quick Help** (the **?** button, top right) is a one-page cheat sheet: the chain order and the first moves for every module. Its **OPEN THE FULL MANUAL** button opens this manual inside Polisher, with contents on the left; it works offline. Press **Esc** or **×** to close it.

**Settings** has three switches:

| Setting | Default | What it does |
| --- | --- | --- |
| Reset loudness meter when playback starts | on | Integrated restarts each time you press play. |
| Restart loudness after you change a control | on | Integrated restarts a moment after a change, so it measures what you hear now. |
| Adaptive release (Maximizer) | on | Same as the Maximizer's Release Mode. |

## Meters

The footer meters always measure the **master** (what leaves Polisher), using the streaming standard (ITU-R BS.1770 / EBU R128).

| Meter | Unit | What it tells you | Good range (trap / hip-hop) |
| --- | --- | --- | --- |
| Integrated | LUFS | Average loudness since it started. The number streaming services read. | −14 (streaming) to about −8 (loud) |
| Short-term | LUFS | Loudness of the last 3 seconds. Watch it while you turn Drive. | Hook within about 1 LU of your target |
| True Peak | dBTP | Highest peak, including between samples. | At or below the Ceiling (−1) |
| Range | LU | How much loudness changes through the song. | Low numbers = a steadier, denser master |
| Output L / R | dB | Peak level per channel. | Below the ceiling |

Spotify and YouTube turn every song to about −14 LUFS. Going louder than that buys density and attitude, not more volume on streaming.

## Troubleshooting

| Problem | Likely cause | Fix |
| --- | --- | --- |
| Master is too loud or squashed even at Drive 0; input reads far above 0 dBTP | Your old chain (EQ, saturator, compressor, limiter) is still on Stereo Out before Polisher, so the mix arrives already limited and hot | Bypass every other insert on Stereo Out, keep Polisher in the last slot, and run Auto-Master again |
| Input is hot even with nothing else on Stereo Out | The mix itself is too loud | Turn the mix down about 6 dB before Polisher (aim for about −17 LUFS, peaks near −1 dBTP). Drive can't go below 0 dB, so Polisher can't turn a too-hot mix down for you |
| Auto-Master says "This doesn't sound like a full mix" | Polisher is on one track, not Stereo Out | Remove it from the track, load it in Stereo Out's last insert slot (F3 > Stereo Out), play the chorus and run Auto-Master again |
| Auto-Master doesn't finish or says it heard too little | The song stopped or was mostly silence | Keep the loudest part playing until it's done (it needs 12 s of music, at least 3 s) |
| Master lands quieter than the target | Auto-Master protected the drums within the intensity's limit | Fine as it is. To go louder, choose Punchy, or tame the drum peaks in the mix |
| B sounds better but you're not sure why | B is louder | Turn on MATCH and play B for 10 s before comparing |
| Kick or snare sounds weaker than the mix | Too much limiting | Lower Drive 1–2 dB, or turn on the Soft Clipper so it takes some peaks |
| Learn says Diminishing returns | Drive past the point where it adds loudness | Lower Drive until Short-term starts to drop, then back up 0.5 dB |
| Harsh or crunchy top | Saturation, clipping or a big EQ boost | Lower Saturation Drive 3 dB or Mix to 30 %; raise Clip Level or Softness; keep EQ moves to 1–3 dB |
| Bass disappears on a phone or in mono | Stereo bass or too much width | Mono Below at 100 Hz; Imager Lows at 100 %; keep Correlation above +0.3 |
| Unlimiter does nothing | Your mix is open (Squash 12.5 dB or more) | Working as intended: it only restores audio that was squashed |
| Integrated keeps restarting | The Settings switches restart it on play and after changes | Turn off those switches in Settings if you want one long reading |
| Polisher isn't in the plug-in list | Cubase hasn't scanned it | Check the VST3 folder, then Studio > VST Plug-in Manager > Update |
| Polisher opens a small screen that says BASIC VIEW | The Microsoft Edge WebView2 Runtime is missing on this PC | Click GET WEBVIEW2, install the runtime, close and reopen Polisher. Audio keeps working meanwhile |

## FAQ

**Which target should I pick?** Stream −14 for the most natural sound on Spotify and YouTube. Loud −9 is common for trap and hip-hop. Club −7 is for DJ and club playback, not streaming.

**Do I have to use Auto-Master?** No. Every module works by hand. Auto-Master is a fast, explained starting point.

**Will Auto-Master change my Stem EQ?** Never. It may suggest a Vocals move with an APPLY button; you decide.

**Why −1 dBTP for the ceiling?** Streaming services convert to MP3/AAC, which can push peaks up. −1 dBTP leaves room so the streamed version doesn't clip.

**Can I trust my headphones?** For tone and punch, yes, if you use MATCH and moderate volume. For loudness, trust the meters. Check on a phone and in a car too.

**Can I put Polisher on a beat or a single track?** On a finished beat, yes (the Unlimiter is made for loud beats). On one instrument or vocal, no: put it on Stereo Out.

**Does Polisher add delay?** A little (Stem EQ about 40 ms). Cubase compensates it automatically, and the export lines up.

**Is my project saved with Polisher's settings?** Yes. All settings, including genre, intensity and target, are saved with the Cubase project.

## Glossary

| Word | Meaning |
| --- | --- |
| LUFS | Loudness the way ears hear it. Higher (closer to 0) = louder. |
| dBTP | True peak: the highest point of the waveform, including between samples. |
| Headroom | Space between your loudest peak and 0 dB. |
| Limiter | Holds peaks under a ceiling so the rest can be turned up. |
| Clipper | Shaves peaks off instantly; on drums it sounds like punch. |
| Compression | Turns loud moments down to even out levels. |
| Mono | The same sound in both speakers. Bass is best mono. |
| Correlation | How mono-safe the stereo is: +1 mono, 0 wide, below 0 parts cancel. |
| Harmonics | Extra overtones that make sound feel fuller and help bass on small speakers. |
| Squash | How flattened audio is: peaks minus the average level. |
