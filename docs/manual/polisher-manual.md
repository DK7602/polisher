# Polisher User Manual

Version 0.15.0 · October 2026

Live, shareable copy: https://claude.ai/code/artifact/3bf99dd7-1111-4be3-884d-d787ae49506c (this file is its source; keep both in step).

## What Polisher is

Polisher is a mastering plug-in: it takes your finished stereo mix and makes it louder, clearer and ready to release. It holds eleven modules in one window, and **Auto-Master** can set all of them for you in about 12 seconds.

This manual covers version 0.15.0. Every control is explained in plain words, with the number ranges the plug-in really uses.

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
- **Double-click the number** under a knob to type an exact value, then press **Enter** (**Esc** cancels). Units are optional: `-3`, `1.2k` for 1.2 kHz, `2.5` for a 2.5:1 ratio, `off` for a control's lowest setting. Values outside the range stop at its end. With a knob selected, **Enter** opens the box too.
- The **mouse wheel** works too.

### Exporting

Export as usual with **File > Export > Audio Mixdown**. MATCH and the listening aids (Listen, Solo, MONO, PHONE) switch themselves off during a normal (offline) export, so your file is the real master. For a Realtime Export, switch MATCH and every listening aid off (no red banner on the spectrum) and make sure B is on before you start: in real time Polisher can't tell an export from playback. Polisher reports its delay to Cubase, which lines everything up for you.

## The signal chain

The order is fixed, from repair to loudness: fix the mix's dynamics first, then tone, then low end, compression and width, then colour, and finally peaks and loudness.

```
Input Trim -> 01 Unlimiter -> 02 Tonal EQ -> [03 Stem EQ / 04 Tone Steady / 05 Resonance Tamer] -> 06 Bass Control
-> 07 Dynamics -> 08 Stereo Imager -> 09 Saturation -> Drive -> 10 Soft Clipper -> 11 Maximizer -> your master
(03-05 share one stage, ~40 ms; MATCH, Listen / Solo and the MONO / PHONE checks sit at the very end, for listening only)
```

The Maximizer's **Drive** sits just before the Soft Clipper, so the clipper and limiter share the peak work. Its **Input Trim** (on the Maximizer screen) sits before everything: it turns a mix that is already too loud down before the first module. MATCH only changes what you hear while comparing; it is off during export.

## Auto-Master

Auto-Master listens to 12 seconds of your song, then sets every module except Stem EQ and explains each choice. It skips silence while it listens, and needs at least 3 seconds of music.

### The three choices

| Choice | Options | What it changes |
| --- | --- | --- |
| Genre | Trap / Hip-Hop, R&B, Pop, EDM, Rock, Acoustic | The tone balance it aims for, the saturation style and the usual vocal level |
| Intensity | Gentle, Balanced, Punchy | How far it may move the EQ and how hard it may squash peaks |
| Target | Stream −14, Loud −9, Club −7, or **Match ref** | The loudness it aims for, in LUFS (with Match ref: the reference song's) |

| Intensity | Largest EQ move | Peak control allowed | Character |
| --- | --- | --- | --- |
| Gentle | 1.5 dB | 2 dB | Most transparent; no Soft Clipper, no kick Punch |
| Balanced | 3 dB | 3.5 dB | The everyday choice |
| Punchy | 4 dB | 5.5 dB | Louder and harder; more Punch and clipping |

Auto-Master sets the Tonal EQ bands together. Neighbouring bands overlap, so it works out the gains whose combined curve moves each range by the amount it wants, instead of setting each band as if it were alone (which over-boosts the middle bands when several move the same way). A band whose range its neighbours already cover stays at 0 dB, and the report says so.

### How to run it

1. Set Genre, Intensity and Target.
2. Play the **loudest part** of the song (hook, chorus or drop).
3. Click **Auto-Master** and let it play until it finishes.
4. Read the report in Learn (**WHY IT SOUNDS LIKE THIS**).
5. Don't like it? Click the **undo arrow** to put every control back as it was.

### What to expect

- If your mix is **already louder than the target** (for example a −8 LUFS beat with Stream −14 picked), it turns the mix down with **Input Trim** before the chain, then sets every module for that level, so the master lands on the target instead of overshooting it.
- It may stop **short of the target** on purpose. If reaching it would crush the drums past the intensity's limit, it keeps the punch and tells you. On a trap mix at about −17 LUFS in, Loud −9 typically lands near −9.7 LUFS.
- A module marked **Checked · kept** was measured and needed no change.
- **Stem EQ** is never changed. If your vocal sits unusually low or loud for the genre, it suggests a Vocals move (up to ±3 dB) with an **APPLY** button. The Tonal EQ comes before Stem EQ, so Auto-Master sets it for your mix without your Stem moves and never EQs them back out; everything after Stem EQ is set for the mix with them.
- If you change a module after Auto-Master, Learn notes that its reasons no longer describe what you hear.
- If the audio doesn't sound like a full mix (for example, Polisher is on one track), Auto-Master changes nothing and tells you to move it to Stereo Out.

Every warning in the report says **Do I need to fix it?** and gives numbered steps. Auto-Master is a starting point: your ears and MATCH make the final call.

### Match ref: master toward a song you love

Instead of a genre's typical sound, Auto-Master can aim for a **reference**: a released song you want your master to sound like.

1. Pick a commercial song with the sound you want (for trap: a big trap single). Use a good file of the released version: WAV, AIFF, FLAC or MP3.
2. Click **Match ref** (at the end of the TARGET row) and choose the file. Polisher reads the whole song (up to 15 minutes) and measures its **loudest 30 seconds**, usually the chorus or drop. That takes a few seconds; your music keeps playing.
3. The pill now shows the song's name, and the spectrum shows its tone as a **dashed yellow line** (moved to the loudness of what's playing, so you compare tone, not volume).
4. Play the **loudest part of your own song** and click **Auto-Master**, as usual.

What it takes from the reference:

| From the reference | What Auto-Master does with it |
| --- | --- |
| Tone (the 7 EQ ranges) | Moves your tone toward it, part of the way, within the same Intensity limits as for a genre |
| Loudness of its loudest part | Aims for it, kept between −16 and −6 LUFS. If getting there would crush your drums, it stops earlier and says so |
| Stereo width of the mids and highs | Widens or narrows them toward it with the Stereo Imager (the lows are never widened) |
| Low end | Mono Below: if the reference keeps stereo in its bass, only the deep sub below 60 Hz is made mono. If its 808 reaches lower or sits higher than yours, the report tells you how to fix that in the mix |

The **genre still decides** what a reference can't show: how much compression, saturation, clipping and kick punch suit the style. The ceiling follows the reference's loudness the way a target would: −1.5 dBTP for references between about −12 and −8 LUFS (like Loud −9), −1 dBTP otherwise.

The report starts with **Matched to your reference "…"**, explains every move against it, and shows a table: **REF / MIX / MASTER** for loudness, deep sub vs bass, bass stereo and width, and the tone difference from the reference in each range. Green means the master is close to the reference, red means it is still far off. Auto-Master closes part of each gap **on purpose**: copying a reference exactly would fight your own mix.

- Only the reference's measurements and its name are saved with your project, not the song. Moving or deleting the file later changes nothing.
- While a reference is loaded the target pills step back; click the **×** on the reference pill to clear it and go back to the genre and target.
- Clicking the pill again loads a different song.

## Every module and its controls

Click a module in the **Signal Chain** (left) to open it. Each module has its own on/off switch (the button at the top right of the module panel, for example **UNLIMITER ON**), and every module is an exact pass-through at its neutral settings, so a module you don't use changes nothing. The Maximizer's button is different: **BYPASS** there bypasses the whole chain, the same as **A**.

### 01 Unlimiter

Puts back the punch a limiter or clipper squashed out of a beat, loop or mix. It lifts the start of each hit; the level between hits stays put. Every clear hit gets about the same lift, so identical kicks come back alike. It can't rebuild a waveform a clipper flattened, but it restores the contrast between hits and the rest.

| Control | Range (default) | What it does |
| --- | --- | --- |
| Amount | 0–100 % (0) | How far each hit is lifted: up to 10 dB at 100 % on brickwalled audio, less on audio that measures less squashed. On a brickwalled beat, 50 % wins back about a third of the lost punch and 100 % about half (before the Maximizer). Start at 75 %. |
| Length | 5–50 ms (15) | How long each lift is held before it lets go. Shorter if vocals or pads jump on each hit. |
| Focus | Full, Kick, Snap | Which hits it listens for and what it lifts: every hit (the whole sound), kick and 808 (only the band below 150 Hz), or snares and hats (only the band above 1 kHz). |
| Squash meter | open … wall | How flattened the incoming audio is: true peak minus the average level, plus a check that the peaks press flat against one ceiling. 12.5 dB or more = open (it rests); 8.5 dB or less = brickwalled (full strength). A loud 808 alone doesn't count as squashed. After Auto-Master it holds the reading Auto-Master took, so playback and export lift the same from the first bar. |
| Restoring meter | 0–10 dB | The lift right now. |

On an open, unmastered mix the Unlimiter rests on purpose, even with Amount up. Use it on beats or loops that arrive already loud and flat.

The Maximizer makes the song loud again afterwards and presses part of the restored punch back down. The closer your target is to how loud the audio already arrives, the less survives: on a beat at −8 LUFS it pays off for Stream −14, little for Loud −9 and not at all for Club −7. Auto-Master tries it at your target and leaves it off when it wouldn't help. Judge it at the final loudness with **MATCH** on.

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

Every band keeps its true shape right up to 20 kHz, at 44.1 and 48 kHz too, so the curve on the spectrum is what you hear. Air set high is still a subtle move: at 44.1 or 48 kHz the shelf above about 16 kHz only lifts the last few kHz before the top of the file, where most people hear little. For audible sheen, keep Air between 10 and 14 kHz.

**Subsonic filter.** While Low End is boosted, a gentle high-pass at 15 Hz fades in by itself, so the boost doesn't also pump up rumble below 20 Hz that no speaker plays but the Maximizer still has to make room for. It costs less than 0.5 dB at 20 Hz and nothing you can measure from 30 Hz up. With Low End at 0 dB or cut, it's out of the path.

On the spectrum, the white line is your mix, the blue curve is the EQ change and the red line is the master after the whole chain.

### 03 Stem EQ

Turns the **Vocals, Drums, Bass** (808 included) or **Other** (synths, keys, guitars, pads, effects) up or down inside your finished stereo mix, like moving one fader back in the session. A small AI model works out, moment by moment and frequency by frequency, how much of the sound is each part.

| Control | Range (default) | What it does |
| --- | --- | --- |
| Vocals / Drums / Bass / Other | ±12 dB (0) | That part's level. 1–3 dB is usually all you need; up to about ±6 dB stays clean. |
| Keep Loudness | on / off (on) | Your moves change the balance, not the loudness. The line under the switch shows how much it is turning the whole mix down (or up) right now, e.g. "Keeping level: −4.0 dB". |
| Model Hears | meter | How the model divides what you hear right now (each frequency counts by how loud it sounds, so a big 808 doesn't fill the whole meter). |

**Keep Loudness.** Turning a part up also makes the mix louder, and louder always sounds better in a quick A/B. With Keep Loudness on, Stem EQ measures how much louder (or quieter) your moves make the mix, the way a loudness meter hears it, and turns the whole mix down (or up) by that much, so the loudness stays within about half a dB. It listens over about 2 seconds, so it doesn't pump with the beat, and a knob move is made up at once. It never moves the level more than 12 dB. Songs saved before this switch existed open with it off, so they sound exactly as before; new Polisher instances start with it on.

Big Bass boosts: in a trap mix the 808 is a large part of the loudness. At the same loudness, more 808 always means less of everything else, with or without Keep Loudness. On a bass-heavy test mix at Bass +6, the master at −9 LUFS came out the same both ways: the 808 band up about 1 dB, 300 Hz–16 kHz down about 4.5 dB (a 5.5 dB shift in balance). What Keep Loudness changes is where the turning down happens: inside Stem EQ, cleanly, instead of in the Maximizer. With Drive left where it was, the Maximizer limited 4.4 dB instead of 8.7 dB at Bass +6, and 4.1 dB instead of 13.7 dB at Bass +12. With Keep Loudness off, a big Bass move pushes the Maximizer much harder; run Auto-Master again (or lower Drive) and compare with MATCH.

Auto-Master never changes the Stem EQ controls (Keep Loudness included); it hears your mix with them, so its loudness search sets Drive for the mix as Stem EQ plays it.

Trade-offs: it adds about 40 ms of delay at any sample rate (Cubase compensates it). Where two parts share the same notes, a move nudges both a little; Other moves touch the drums slightly.

### 04 Tone Steady

Keeps the tone consistent through the song. When a section drifts muddier, harsher or thinner than the song's own balance, it nudges it back. It doesn't change your overall sound; that's the Tonal EQ's job.

| Control | Range (default) | What it does |
| --- | --- | --- |
| Amount | 0–100 % (0) | How much drift is corrected. 40 % is a good start. |
| Range | 1–6 dB (3) | Largest cut; boosts go up to half of it. |
| Speed | Slow, Medium, Fast (Medium) | How fast it reacts: about 1 s, 0.4 s or 0.15 s. |
| Steadying meter | 0–6 dB | The biggest move right now. |

It learns your song's balance over about 10 seconds, leaves quiet breakdowns alone, and is gentle on the top end so busy hi-hats keep their sparkle. When a bass or 808 comes in (the low end jumps more than 4 dB against the rest, say from the intro into the chorus), it re-learns the balance within about a second instead of cutting the new low end. Range is the most it can move any part of the spectrum, so a section meant to sound different can be pulled by up to that much: if the drop feels smaller, lower Range (to about 1.5 dB) or Amount.

### 05 Resonance Tamer

Finds ringing tones that stick out (boxiness, whistly vocal tones, harsh hats) and cuts them, only while they ring. Above about 1 kHz the cuts are narrow; below about 800 Hz each cut is roughly 3/4 octave wide, so it takes some of the sound around the ring with it (vocal body included, in the Boxiness range).

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
| Sub Tame | 0–100 % (0) | Turns down 808 notes that jump above the song's usual low end (up to 12 dB, below about 120 Hz). It listens for about a second before it cuts anything. |
| Punch | 0–100 % (0) | Adds up to 4 dB of thump (around 80 Hz) to the start of each kick, without raising the sub. It spots kicks by the click at their start, so 808 notes on their own aren't boosted (and a kick with no click gets little). |
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
| Attack | 0.5–100 ms (30 / 20 / 10 / 5) | Slow lets drum hits through; fast catches peaks. The Low band never reacts faster than about 20 ms, so bass notes don't wobble. |
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
| Oversampling | 1x, 2x, 4x, 8x (4x) | Cuts down harsh aliasing. 4x is the mastering default; 8x is cleanest. |
| Clipping meter | 0 to −6 dB | Peak reduction. Aim for 1–3 dB on the drums. |

### 11 Maximizer

The final true-peak limiter. It makes the master loud and guarantees nothing goes over the ceiling.

| Control | Range (default) | What it does |
| --- | --- | --- |
| Drive | 0–24 dB (0) | Pushes the mix into the limiter. More Drive = louder. |
| Input Trim | −24–0 dB (0) | Turns your mix down **before the whole chain**. Drive only pushes up, so this is how Polisher masters a mix that already arrives louder than your target. Auto-Master sets it when needed; at 0 dB the mix goes in as it is. A (bypass) ignores it. |
| Ceiling | −3 to 0 dBTP (−1) | The highest peak allowed, between samples too. −1 suits most masters; loud ones (−9 LUFS and up) are safer at −1.5 to −2, because their MP3/AAC copies peak higher. |
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
- On B the meters show the **real master**, never the matched level. On A they show your original mix.
- MATCH switches itself off when you close Polisher and during export.

On headphones, compare at a low, comfortable volume and take breaks. Ears tire fast, and tired ears like everything brighter.

### Blind test: compare masters without knowing which is which

MATCH takes care of loudness, but you still know which side is Polisher. To compare Polisher's master with another one (your old chain, another plug-in, a pro master of the same song) fairly, use **Polisher Blind Test** (`PolisherBlindTest.exe`, in the same download as the plug-in):

1. In Cubase, export each version of the same song with **File > Export > Audio Mixdown** (WAV), for example: your mix with Polisher bypassed (`my mix.wav`), Polisher's master (`polisher.wav`), and the other master (`other.wav`). Two to eight files.
2. Drag all the files at once onto **PolisherBlindTest.exe** (in the **BlindTest** folder of the download).
3. It measures each file, turns the louder ones **down** to the quietest one's loudness (never up, never into clipping), shuffles them and saves them as **Sample A**, **Sample B**, ... in a new folder next to your files.
4. Listen in any player at one fixed volume. Jump between the samples at the same moment of the song and fill in **Score sheet.txt** (score 1–10 and notes for each).
5. Only then open **Answer key.txt** to see which sample was which.

To compare just one part (say 30 seconds from the hook at 1:00), run it from a command prompt: `PolisherBlindTest.exe "my mix.wav" polisher.wav --start 60 --length 30` (quotes around names with spaces).

## Listening aids: Listen, Solo, MONO and PHONE

These change only what you **hear**, never your master: they are not saved with the song, they switch off when you close Polisher, and a normal export never contains them. While any of them is on, a **red banner** across the top of the spectrum says what you are hearing ("LISTENING: what Saturation adds — not the master"); **BACK TO MASTER** on the banner turns everything off. The meters keep measuring the real master the whole time.

### Listen: hear only what a module changes

Five modules have a headphones button, **LISTEN**, at the top right of their panel. It plays only what that module adds or takes away, so you learn what it really does to your song. One at a time: pressing another one switches over (with a short gap); pressing it again goes back to the master. Every switch fades over about 15 ms, so nothing clicks.

| Module | LISTEN plays | What to listen for |
| --- | --- | --- |
| Saturation | Only the harmonics it adds (output minus input, lined up to the sample) | A soft, buzzy shadow of the 808 and vocal is normal: that's the warmth and the harmonics that carry the 808 on phones. Clear crackle or fizz on the hits: lower Drive or Mix. |
| Soft Clipper | What the clipping shaves off the peaks (the input at the clipper's level, minus its output) | Short ticks on the kick and snare are normal. A buzz on the 808, the snare's body or anything like a note: raise Clip Level or Softness. |
| Resonance Tamer | The ringing tones it cuts | Thin whistles, rings or a boxy honk are what it should catch. Whole vocal or melody notes: lower Amount or Sensitivity. |
| Maximizer | What the limiter takes off the peaks | Faint ticks are normal. Clear kicks and snares, or a pumping 808: lower Drive 1–2 dB or turn on the Soft Clipper. |
| Dynamics | What the picked band's compression takes off (before its Gain) | Short bumps on the loudest hits are normal. The band most of the time, or a pumping 808: raise Threshold or lower Ratio. |

**Dynamics also has S (solo) buttons**, one per band: S plays only that band (after its compression, at its real level in the master), so you hear what lives in it. Move a band's Split until the kick and 808 sit in LOW and the vocal's body in LO-MID. The four solos add up to exactly what Dynamics puts out.

What a module adds or removes is often 20–40 dB quieter than the music, too quiet to hear at your normal volume. So Polisher **turns quiet changes up** (toward 15 dB under the master, at most +24 dB) and says so on the banner ("+18 dB so you can hear it"); Learn tells you how far under the music it really sits ("In your master it sits about 33 dB under the music"). Judge the **character** (ticks or crackle, a ring or a note), and remember the real level. A change that would peak over the ceiling is turned down instead ("kept under the ceiling"); a solo plays at its real level. Each change is lined up with the master to the sample and given the same Drive and limiting it gets in the master. On **A** nothing is processed, so a Listen is silent there.

### MONO and PHONE: check how your master translates

Next to MATCH, two small buttons check your master on other speakers. They work with A / B and MATCH (MATCH still compares fairly: both sides get the same check).

- **MONO** plays left and right mixed together on both speakers, like a club PA or a phone. If a part gets much quieter or disappears (often wide synths, reverbs or doubled vocals), it is out of phase and will vanish on mono speakers: bring the Stereo Imager's Mids and Highs toward 100 %, or narrow that part in the mix.
- **PHONE** plays your master like a phone speaker: mono, almost nothing below about 300 Hz or above 8 kHz (gentle slopes), with a small bump around 2 kHz, at about the same loudness on a balanced mix (a mix whose weight is all sub gets quieter, as on a real phone). **Does the 808 still come through?** You hear its upper harmonics, not its sub; that's normal, and it's what Saturation (Tube is the fullest) and an 808's own distortion are for. If the 808 disappears, add a little Saturation Drive or 1–2 dB of Bass on the Tonal EQ, or saturate the 808 track in the mix.

While any check or Listen is on, nothing leaves Polisher above 0 dBFS.

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

The footer meters always measure the **master** (what leaves Polisher), using the streaming standard (ITU-R BS.1770 / EBU R128), also while MATCH, a Listen, Solo, MONO or PHONE is on.

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
| Input is hot even with nothing else on Stereo Out | The mix itself is too loud | Run Auto-Master: it turns the mix down with Input Trim when it's louder than your target. Or lower Input Trim yourself (Maximizer screen) by about as many dB as the master's Integrated reads above your target |
| Auto-Master says "This doesn't sound like a full mix" | Polisher is on one track, not Stereo Out | Remove it from the track, load it in Stereo Out's last insert slot (F3 > Stereo Out), play the chorus and run Auto-Master again |
| Auto-Master doesn't finish or says it heard too little | The song stopped or was mostly silence | Keep the loudest part playing until it's done (it needs 12 s of music, at least 3 s) |
| Master lands quieter than the target | Auto-Master protected the drums within the intensity's limit | Fine as it is. To go louder, choose Punchy, or tame the drum peaks in the mix |
| Master isn't as loud as the reference | The reference had its drum peaks controlled in the mix; reaching it would crush yours | Fine as it is (streaming plays both at the same level). To get closer: a clipper on the drum bus, or Punchy |
| Match ref says it can't read the file | Not WAV, AIFF, FLAC or MP3, or a damaged file | Export or convert the song to WAV and load that |
| B sounds better but you're not sure why | B is louder | Turn on MATCH and play B for 10 s before comparing |
| Polisher sounds thin, mono, crackly or like a phone; a red banner is on the spectrum | A listening aid is on (Listen, Solo, MONO or PHONE) | Click BACK TO MASTER on the banner. They switch off by themselves when you close Polisher and are never in a normal export |
| Listen plays silence | The module isn't changing anything right now (or you're on A) | Fine: nothing to hear. Play a louder part, switch to B, or set the module up first |
| Kick or snare sounds weaker than the mix | Too much limiting | Lower Drive 1–2 dB, or turn on the Soft Clipper so it takes some peaks |
| Learn says Diminishing returns | Drive past the point where it adds loudness | Lower Drive until Short-term starts to drop, then back up 0.5 dB |
| Harsh or crunchy top | Saturation, clipping or a big EQ boost | Lower Saturation Drive 3 dB or Mix to 30 %; raise Clip Level or Softness; keep EQ moves to 1–3 dB |
| Bass disappears on a phone or in mono | Stereo bass or too much width | Mono Below at 100 Hz; Imager Lows at 100 %; keep Correlation above +0.3 |
| Unlimiter does nothing | Your mix is open (Squash 12.5 dB or more) | Working as intended: it only restores audio that was squashed |
| Auto-Master left the Unlimiter off on a squashed beat | Your target is about as loud as the beat already is, so the Maximizer would press the hits straight back down | Working as intended. Pick a quieter target (Stream −14) if you want the punch back |
| Integrated keeps restarting | The Settings switches restart it on play and after changes | Turn off those switches in Settings if you want one long reading |
| Polisher isn't in the plug-in list | Cubase hasn't scanned it | Check the VST3 folder, then Studio > VST Plug-in Manager > Update |
| Polisher opens a small screen that says BASIC VIEW | The Microsoft Edge WebView2 Runtime is missing on this PC | Click GET WEBVIEW2, install the runtime, close and reopen Polisher. Audio keeps working meanwhile |

## FAQ

**Which target should I pick?** Stream −14 for the most natural sound on Spotify and YouTube. Loud −9 is common for trap and hip-hop. Club −7 is for DJ and club playback, not streaming.

**Do I have to use Auto-Master?** No. Every module works by hand. Auto-Master is a fast, explained starting point.

**Should I use Match ref or a genre?** A genre is a safe average. A reference is better when you know exactly whose sound you want. Pick a reference in the same style as your song: a trap reference for a trap song.

**Can a Listen, Solo, MONO or PHONE end up in my export?** Not in a normal (offline) export: Polisher switches them off. They are not saved with the song and switch off when you close Polisher. Only a Realtime Export plays what you hear, so check that the red banner is gone first.

**Will Auto-Master change my Stem EQ?** Never. It may suggest a Vocals move with an APPLY button; you decide.

**Why −1 dBTP for the ceiling?** Streaming services convert to MP3/AAC, which pushes peaks up, and the louder the master the more. −1 dBTP leaves enough room for most masters; Auto-Master uses −1.5 for Loud −9 (Spotify asks for −2 dBTP on masters louder than −14 LUFS). No ceiling makes clipping after conversion impossible, only unlikely.

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
| Squash | How flattened audio is: peaks minus the average level, read as open when the peaks don't press against one ceiling. |
