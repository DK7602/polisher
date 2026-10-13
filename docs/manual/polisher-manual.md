# Masterology User Manual

Version 0.25.0 · October 2026

Live, shareable copy: https://claude.ai/code/artifact/3bf99dd7-1111-4be3-884d-d787ae49506c (this file is its source; keep both in step).

## What Masterology is

Masterology (called **Polisher** before version 0.25.0) is a mastering plug-in: it takes your finished stereo mix and makes it louder, clearer and ready to release. It holds twelve modules in one window, and **Auto-Master** can set all of them for you while your song plays once.

This manual covers version 0.25.0. Every control is explained in plain words, with the number ranges the plug-in really uses.

**The 30-second version**

1. Put Masterology in the **last insert slot of Stereo Out**.
2. Turn off any other limiter, clipper or mastering chain on Stereo Out.
3. Pick your genre, an intensity and a target at the top.
4. Press **Auto-Master** and play your whole song from the start. It finds the loudest part by itself and finishes when the song ends.
5. Turn on **MATCH** and flip **A / B** to check the result at equal loudness.
6. Read the **Learn** panel on the right: it explains every choice and tells you if anything needs fixing.

## Getting started in Cubase

Masterology goes on **Stereo Out**, the channel every track flows into, in the **last insert slot**. On a single track it can't master anything, and Auto-Master will refuse and tell you so.

### Install

1. Download the Windows build (Masterology-Windows) and unzip it.
2. Copy **Masterology.vst3** into `C:\Program Files\Common Files\VST3`.
3. **Updating from Polisher** (Masterology's old name)? Delete **Polisher.vst3** from that folder first. It's the same plug-in, so your projects that used Polisher open with Masterology and keep their settings.
4. Start Cubase. If Masterology doesn't show up, open **Studio > VST Plug-in Manager** and click **Update Plug-in Information**.

Masterology's screen runs on the **Microsoft Edge WebView2 Runtime**. Windows 11 has it built in and Windows 10 gets it with Microsoft Edge, so you rarely need to do anything. If Masterology opens a small screen that says **BASIC VIEW**, the runtime is missing: click **GET WEBVIEW2**, install the free "Evergreen" runtime from Microsoft, then close and reopen Masterology. Your settings are kept, and the whole chain keeps processing audio in the basic view.

### Load it on Stereo Out

1. Press **F3** to open the MixConsole.
2. Find **Stereo Out** on the far right.
3. Click the **last insert slot** and choose Masterology.
4. Bypass every other plug-in on Stereo Out that changes loudness or tone (limiters, clippers, compressors, mastering EQs). Masterology replaces that chain; stacking two chains makes the input far too hot.

### Set your mix level

Leave headroom going in. A good input is about **−17 LUFS integrated with peaks around −1 dBTP**. If your mix is hotter, pull it down (for example, lower the Stereo Out fader before Masterology, or turn down the mix bus by about 6 dB).

### Your first master

1. Click the genre name at the top and choose yours (Trap / Hip-Hop, R&B, Pop, EDM, Rock, Acoustic).
2. Choose **Intensity**: Gentle, Balanced or Punchy. Balanced is the default.
3. Choose **Target**: Stream −14, Loud −9 or Club −7. Loud −9 is the default.
4. Click **Auto-Master**, then play your song from the start. It finishes by itself 4 seconds after the song ends (or click it again any time after 12 seconds to use what it heard).
5. Turn on **MATCH**, flip **A / B** and listen.
6. Read Learn. Red boxes need a decision; blue boxes are tips.

### Using the knobs

- Drag a knob **up or down**. Hold **Shift** for fine moves.
- **Double-click** a knob to reset it.
- **Double-click the number** under a knob to type an exact value, then press **Enter** (**Esc** cancels). Units are optional: `-3`, `1.2k` for 1.2 kHz, `2.5` for a 2.5:1 ratio, `off` for a control's lowest setting. Values outside the range stop at its end. With a knob selected, **Enter** opens the box too.
- The **mouse wheel** works too.
- Changed your mind? The **undo arrow** at the top (or **Ctrl+Z**) takes back your last change; see [Undo, redo and snapshots](#undo-redo-and-snapshots).
- **Space and Enter** never press a Masterology button a second time after you clicked it, so pressing Space to play the song can't cancel Auto-Master or switch MATCH off. If Space doesn't start Cubase while Masterology's window is in front, click Cubase's project window first. Moving around with **Tab**, Space and Enter press buttons as usual.
- Drag the window's corner to resize it; everything scales together. It doesn't go smaller than 1120 × 630, where the small print would be too small to read.

### Exporting

Export as usual with **File > Export > Audio Mixdown**. MATCH and the listening aids (Listen, Solo, MONO, PHONE) switch themselves off during a normal (offline) export, so your file is the real master. For a Realtime Export, switch MATCH and every listening aid off (no red banner on the spectrum) and make sure B is on before you start: in real time Masterology can't tell an export from playback. Masterology reports its delay to Cubase, which lines everything up for you.

## The signal chain

The order is fixed, from repair to loudness: fix the mix's dynamics first, then tone, then low end, compression and width, then colour, and finally peaks and loudness.

```
Input Trim -> 01 Unlimiter -> 02 Tonal EQ -> [03 Stem EQ / 04 Tone Steady / 05 Resonance Tamer] -> 06 Bass Control
-> 07 Glue -> 08 Dynamics -> 09 Stereo Imager -> 10 Saturation -> Drive -> 11 Soft Clipper -> 12 Maximizer -> your master
(03-05 share one stage, ~40 ms; MATCH, Listen / Solo and the MONO / PHONE checks sit at the very end, for listening only)
```

The Maximizer's **Drive** sits just before the Soft Clipper, so the clipper and limiter share the peak work. Its **Input Trim** (on the Maximizer screen) sits before everything: it turns a mix that is already too loud down before the first module. MATCH only changes what you hear while comparing; it is off during export.

## Auto-Master

Auto-Master listens to your whole song as it plays, finds its **loudest part** by itself (the 12 seconds that measure loudest, usually the hook or drop), then sets every module except Stem EQ for that part and explains each choice (Glue it switches to 1:1 for now, see Glue). It skips silence and stops while it listens. While it listens it also learns your song's overall tone for **Tone Steady**, and measures the whole song's loudness for the report.

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
2. Click **Auto-Master**. It says **LISTENING**.
3. Play your **whole song**, ideally from the very start. The button shows how much it has heard and where the loudest part is so far; Learn shows the same, with **FINISH NOW** and **CANCEL**.
4. It **finishes by itself** 4 seconds after the song ends: once it has heard at least 30 seconds of music, 4 seconds without music (the music stops, or you stop playback) count as the end. So a pause longer than 4 seconds after the first 30 seconds also ends it; before 30 seconds you can stop and play on. In a hurry? From **12 seconds** of music on (the bar under the button turns green), click Auto-Master again or press **FINISH NOW**: it uses what it has heard. Before 12 seconds a click stops it without changing anything.
5. It then **chooses every setting** (the button says THINKING and counts the seconds). That runs the whole chain many times on the 12 seconds it kept, so it takes longer at higher sample rates: usually 5–15 seconds at 44.1 or 48 kHz, 10–30 seconds at 96 kHz, 20–60 seconds at 192 kHz (slower computers take longer). **CANCEL** in Learn stops it without changing anything, and closing the project doesn't wait for it.
6. Read the report in Learn (**WHY IT SOUNDS LIKE THIS**). It starts with what it heard, for example *Listened to 2:47 of your song; the loudest part (0:54–1:06, usually the hook or drop) set the levels*, and shows the **Whole song** loudness and loudness range as your mix arrives. *Your mix measured …* is your mix as it reaches Masterology, before Input Trim and before your Stem EQ moves (with Keep Loudness off, a Stem EQ boost makes it louder inside Masterology, and the report says so).
7. Don't like it? Click the **undo arrow** to put every control back as it was, Tone Steady's target too (the whole run is one undo step; the redo arrow brings it back). To compare two runs, keep each in a snapshot: see [Undo, redo and snapshots](#undo-redo-and-snapshots).

Timeline positions come from Cubase's playhead. If playback **jumps** while it listens (a **cycle loop** wraps, or you move the playhead), the report says so: when the loudest 12 seconds it heard join two places on your timeline it names both, and the whole-song figures then count what it heard, repeats included. For exact figures, turn Cycle off and play the song once through. It listens for up to 15 minutes, and its memory stays the same however long the song is (it keeps only the loudest 12 seconds of audio and a few numbers per second). On a **mono** bus it measures one channel, as loudness meters do.

### What to expect

- If your mix is **already louder than the target** (for example a −8 LUFS beat with Stream −14 picked), it turns the mix down with **Input Trim** before the chain, then sets every module for that level, so the master lands on the target instead of overshooting it.
- It may stop **short of the target** on purpose. If reaching it would crush the drums past the intensity's limit, it keeps the punch and tells you. On a trap mix at about −17 LUFS in, Loud −9 typically lands near −9.7 LUFS.
- A module marked **Checked · kept** was measured and needed no change.
- **Stem EQ** is never changed. If your vocal sits unusually low or loud for the genre, it suggests a Vocals move (up to ±3 dB) with an **APPLY** button. The Tonal EQ comes before Stem EQ, so Auto-Master sets it for your mix without your Stem moves and never EQs them back out; everything after Stem EQ is set for the mix with them.
- If you change a module after Auto-Master, Learn notes that its reasons no longer describe what you hear.
- If you finished early and a later part of the song plays more than 2 LU louder than the part it heard, the Maximizer's Learn says **LOUDER THAN WHAT AUTO-MASTER HEARD**: that part is limited harder than planned. Run Auto-Master again and play the whole song.
- A quick run (finished before 30 seconds) doesn't replace a Tone Steady target learned from more of the song, as long as what it heard matches that tone. If it doesn't (the target came with a preset or from another song), the tone of what it heard replaces it.
- If the audio doesn't sound like a full mix (for example, Masterology is on one track), Auto-Master changes nothing and tells you to move it to Stereo Out.

Every warning in the report says **Do I need to fix it?** and gives numbered steps. Auto-Master is a starting point: your ears and MATCH make the final call.

### Match ref: master toward a song you love

Instead of a genre's typical sound, Auto-Master can aim for a **reference**: a released song you want your master to sound like.

1. Pick a commercial song with the sound you want (for trap: a big trap single). Use a good file of the released version: WAV, AIFF, FLAC or MP3.
2. Click **Match ref** (at the end of the TARGET row) and choose the file. Masterology reads the whole song (up to 15 minutes) and measures its **loudest 30 seconds** (tone, width, low end), usually the chorus or drop, and the loudness of its **loudest 12 seconds**. That takes a few seconds; your music keeps playing.
3. The pill now shows the song's name, and the spectrum shows its tone as a **dashed yellow line** (moved to the loudness of what's playing, so you compare tone, not volume, and to your project's sample rate). The dotted genre line steps aside while a reference is loaded; click its legend entry to show it too (see [The spectrum screen](#the-spectrum-screen)).
4. Click **Auto-Master** and play **your own song**, as usual.

What it takes from the reference:

| From the reference | What Auto-Master does with it |
| --- | --- |
| Tone (the 7 EQ ranges) | Moves your tone toward it, part of the way, within the same Intensity limits as for a genre |
| Loudness of its loudest 12 seconds | Aims for it with the loudest 12 seconds of your song (hook against hook), kept between −16 and −6 LUFS. If getting there would crush your drums, it stops earlier and says so |
| Stereo width of the mids and highs | Widens or narrows them toward it with the Stereo Imager (the lows are never widened) |
| Low end | Mono Below: if the reference keeps stereo in its bass, only the deep sub below 60 Hz is made mono. If its 808 reaches lower or sits higher than yours, the report tells you how to fix that in the mix |

The **genre still decides** what a reference can't show: how much compression, saturation, clipping and kick punch suit the style. The ceiling follows the reference's loudness the way a target would: −1.5 dBTP for references louder than about −12 LUFS (like Loud −9 and Club −7), −1 dBTP for quieter ones.

The report starts with **Matched to your reference "…"**, explains every move against it, and shows a table: **REF / MIX / MASTER** for loudness, deep sub vs bass, bass stereo and width, and the tone difference from the reference in each range. Green means the master is close to the reference, red means it is still far off. Auto-Master closes part of each gap **on purpose**: copying a reference exactly would fight your own mix.

- Only the reference's measurements and its name are saved with your project, not the song. Moving or deleting the file later changes nothing.
- While a reference is loaded the target pills step back; click the **×** on the reference pill to clear it and go back to the genre and target.
- Clicking the pill again loads a different song.

## Every module and its controls

Click a module in the **Signal Chain** (left) to open it. Each module has its own on/off switch (the button at the top right of the module panel, for example **UNLIMITER ON**), and every module is an exact pass-through at its neutral settings, so a module you don't use changes nothing. The Maximizer's button is different: **BYPASS** there bypasses the whole chain, the same as **A**.

Switching a module on or off (or A / B, a snapshot, undo) fades over smoothly, so nothing clicks or drops out. Mono Below, Dynamics and the Stereo Imager ease their filters into place first: they take about 0.15 s to settle, up to half a second with Mono Below or the lowest Dynamics split set very low.

### 01 Unlimiter

Puts back the punch a limiter or clipper squashed out of a beat, loop or mix. It lifts the start of each hit; the level between hits stays put. Every clear hit gets about the same lift, so identical kicks come back alike. It can't rebuild a waveform a clipper flattened, but it restores the contrast between hits and the rest.

| Control | Range (default) | What it does |
| --- | --- | --- |
| Amount | 0–100 % (0) | How far each hit is lifted: up to 10 dB at 100 % on brickwalled audio, less on audio that measures less squashed. On a brickwalled beat, 50 % wins back about a third of the lost punch and 100 % about half (before the Maximizer). Start at 75 %. |
| Length | 5–50 ms (15) | How long each lift is held before it lets go. Shorter if vocals or pads jump on each hit. |
| Focus | Full, Kick, Snap | Which hits it listens for and what it lifts: every hit (the whole sound), kick and 808 (only the band below 150 Hz), or snares and hats (only the band above 1 kHz). Switching (Focus, on / off, A / B) never dips the sound; Kick and Snap narrow down to their band within about 0.2 s. |
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

**Subsonic filter.** While Low End is boosted, a gentle high-pass at 15 Hz fades in by itself, so rumble and DC below about 12 Hz, which no speaker plays but the Maximizer still has to make room for, are turned down instead of up. Between 12 and 20 Hz part of the boost remains (Low End +6 dB: about +3 dB at 15 Hz and +5 dB at 18 Hz). It costs about 0.5 dB at 20 Hz and nothing you can measure from 30 Hz up. With Low End at 0 dB or cut, it's out of the path.

**Stereo / Mid / Side** (the button under each knob, Stereo by default): what the band works on.

| Setting | Works on | Good for |
| --- | --- | --- |
| STEREO | The whole mix | Normal tone moves |
| MID | Only the centre: vocal, kick, 808, snare | Cut mud (Low Mid −1 dB) or add vocal presence without touching the wide parts |
| SIDE | Only the stereo edges: width, reverbs, wide synths and hats | Air +1 to +2 dB for more width without a brighter vocal; cut harsh wide hats |

Click the button to cycle STEREO → MID → SIDE. The change glides, so it never clicks. Keep Low End and Bass on STEREO or MID: boosting the lows on SIDE makes the bass wide, which costs headroom and collapses on phones (Bass Control's Mono Below undoes it anyway). On a mono track a SIDE band does nothing. Auto-Master sets every band back to STEREO.

On the spectrum, the white line is your mix, the blue curve is the EQ change and the red line is the master after the whole chain. The dotted line (for example **Trap target**) is the tone typical masters in your genre have, the balance Auto-Master's EQ aims for: where the red line sits above it, your master has more of that range than typical (see [The spectrum screen](#the-spectrum-screen)).

### 03 Stem EQ

Turns the **Vocals, Drums, Bass** (808 included) or **Other** (synths, keys, guitars, pads, effects) up or down inside your finished stereo mix, like moving one fader back in the session. A small AI model works out, moment by moment and frequency by frequency, how much of the sound is each part.

| Control | Range (default) | What it does |
| --- | --- | --- |
| Vocals / Drums / Bass / Other | ±12 dB (0) | That part's level. 1–3 dB is usually all you need; up to about ±6 dB stays clean. |
| Keep Loudness | on / off (on) | Your moves change the balance, not the loudness. The line under the switch shows how much it is turning the whole mix down (or up) right now, e.g. "Keeping level: −4.0 dB". |
| Model Hears | meter | How the model divides the mix coming in (before your moves): each part's share of the loudness, weighed like a loudness meter. The share tells you what a move does: with Keep Loudness off, a part with a 70 % share turned up 6 dB makes the mix about 5 dB louder; one with 10 %, about 1 dB. In trap the 808 is often most of the loudness. |

**Keep Loudness.** Turning a part up also makes the mix louder, and louder always sounds better in a quick A/B. With Keep Loudness on, Stem EQ measures how much louder (or quieter) your moves make the mix, the way a loudness meter hears it, and turns the whole mix down (or up) by that much, so the loudness stays within about half a dB. It listens over about 2 seconds, so it doesn't pump with the beat, and a knob move is made up at once. When the song drops into a quieter part (a breakdown without the 808), it catches up within about 5 seconds (on a test mix at Bass +6: about 2.7 dB too quiet over the first 4 seconds, half a dB over the next 4). It never moves the level more than 12 dB. Songs saved before this switch existed open with it off, so they sound exactly as before; new Masterology instances start with it on.

Big Bass boosts: in a trap mix the 808 is a large part of the loudness. At the same loudness, more 808 always means less of everything else, with or without Keep Loudness. On a bass-heavy test mix at Bass +6, the master at −9 LUFS came out the same both ways: the 808 band up about 1 dB, 300 Hz–16 kHz down about 4.5 dB (a 5.5 dB shift in balance). What Keep Loudness changes is where the turning down happens: inside Stem EQ, cleanly, instead of in the Maximizer. With Drive left where it was, the Maximizer limited 4.4 dB instead of 8.7 dB at Bass +6, and 4.1 dB instead of 13.7 dB at Bass +12. With Keep Loudness off, a big Bass move pushes the Maximizer much harder; run Auto-Master again (or lower Drive) and compare with MATCH.

Auto-Master never changes the Stem EQ controls (Keep Loudness included); it hears your mix with them, so its loudness search sets Drive for the mix as Stem EQ plays it.

Trade-offs: it adds about 40 ms of delay at any sample rate (Cubase compensates it). Where two parts share the same notes, a move nudges both a little; Other moves touch the drums slightly. Stem EQ, Tone Steady and the Resonance Tamer do their work in short bursts: at 96 kHz or higher, set your audio buffer (Cubase: Studio Setup, your ASIO driver) to **256 samples or more**, or it can crackle. At 44.1 or 48 kHz the usual buffers are fine.

### 04 Tone Steady

Keeps the tone consistent through the song. When a section drifts muddier, harsher or thinner than the song's own balance, it nudges it back. It doesn't change your overall sound; that's the Tonal EQ's job.

| Control | Range (default) | What it does |
| --- | --- | --- |
| Amount | 0–100 % (0) | How much drift is corrected. 40 % is a good start. |
| Range | 1–6 dB (3) | Largest cut; boosts go up to half of it (and, with your song's tone as the target, cuts above the bass too). |
| Speed | Slow, Medium, Fast (Medium) | How fast it reacts: about 1 s, 0.4 s or 0.15 s. |
| Target | LEARN / CLEAR | What it holds the song to: **your song** (its own overall tone, learned while it played) or **following the music** (the last ~10 seconds). |
| Steadying meter | 0–6 dB | The biggest move right now. |

It leaves quiet breakdowns alone and is gentle on the top end, so busy hi-hats keep their sparkle. Range is the most it can move any part of the spectrum, so a section meant to sound different can be pulled by up to that much: if the drop feels smaller, lower Range (to about 1.5 dB) or Amount.

**Target: your song.** When Auto-Master listens to your whole song, it also learns the song's own overall tone and gives it to Tone Steady (you can also press **LEARN** in this panel and play the song: that only learns the tone and changes nothing else). Tone Steady then holds every part to your song's own tone, so **what you hear is what you export**, from any start point (quiet intros and breakdowns, more than 15 dB under the song, are left alone, judged against the whole song's level). Parts that come and go **in the low end** are left alone: an 808 that enters after the intro is not cut, and the intro without it is not lifted (the low end below 160 Hz is judged as one: more than 4 dB away from the song's tone is the arrangement, not a drift). A part that isn't playing is not faked back (anything else more than 6 dB under the song's tone). A part that comes in **above the bass** (a lead in the chorus) can't be told from a harsher passage, so it is treated like a drift: pulled down for as long as it plays, by at most half the Range (1.5 dB at Range 3). A passage that drifts muddier or harsher is pulled back the same way. When most of the spectrum above the bass rises at once, part of that reads as a thinner low end, which is lifted a little. The target follows your Tonal EQ moves and Input Trim, so a tone change you make there isn't undone (if you change the mix's level before Masterology by more than about 10 dB, press LEARN again). It is saved with your project, and also in presets and copies of Masterology: on a new song, press LEARN again. If the music sits far from the stored tone for about 30 seconds (another song's tone), the Target cell reads "another song?" and Learn explains what to do. **CLEAR** goes back to following the music. If it learned from under 30 seconds of music, Learn suggests learning again from the whole song.

**Following the music** (no target, and every song saved before targets existed): it learns your song's balance over about 10 seconds. When a bass or 808 comes in (the low end jumps more than 4 dB against the rest, say from the intro into the chorus), it re-learns the balance within about a second instead of cutting the new low end. Because it follows the music, what you hear can depend a little on where playback or an export starts.

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

### 07 Glue

One compressor for the whole mix: the drums, the 808 and the vocal move together, so the beat sits as one ("glue"). Dynamics (next) works band by band; Glue works on everything at once, the way a bus compressor on a mixing console does. It's off (Ratio 1:1) until you set it.

| Control | Range (default) | What it does |
| --- | --- | --- |
| Character | Bus, Opto, Tube (Bus) | The flavour, after classic bus compressors. **Bus**: clean and firm, the classic glue on a hip-hop mix. **Opto**: smooth and slow; it never grabs faster than about 10 ms and lets go gently. **Tube**: the softest; it starts working well under the threshold and firms up gently as the music gets louder. |
| Threshold | −40 to 0 dB (−20) | Where it starts holding the mix down. |
| Ratio | 1:1–10:1 (1:1 = off) | How firmly. 2:1 is classic glue, 4:1 is firm. |
| Attack | 0.1–30 ms (10) | How fast it grabs. 10–30 ms lets the start of each kick and snare through (punch); under 3 ms smooths the hits and can roughen the 808. |
| Release | 50–1200 ms (300), AUTO on | How fast it lets go (Opto lets go 1.5x and Tube 1.3x slower than the knob says). **AUTO** (the button under the knob) lets go quickly after a short peak and at the Release time on long loud passages, so it doesn't pump. |
| Ignore Lows | off (all the way left), 21–250 Hz (90) | The compressor hears the low end below this much quieter (24 dB per octave), so the 808 doesn't pull the vocal and hats down with every note (pumping). The low end is still turned down along with everything else. Below about 60 Hz it only keeps the deepest sub out. |
| Mix | 0–100 % (100) | Blend with the uncompressed mix. Lower = parallel ("New York") compression: the hits stay sharp while the quieter parts come up. |
| Gain | ±12 dB (0) | Level after the Glue. Compressing makes the mix a little quieter; you can leave this at 0 and let the Maximizer's Drive make it up. |
| Reduction | meter, 0 to −12 dB | How much Glue is holding the mix down. 1–2 dB is classic glue. |

How to set it: Bus, Ratio 2:1, Attack 10 ms, Release AUTO, Ignore Lows 90 Hz. Play the hook and lower Threshold until the Reduction meter shows 1–2 dB. Compare with MATCH: B should sound more together, not smaller or flatter. At 4 dB or more the beat starts to lose its bounce.

- Turning Glue on or off, or Ratio up from 1:1, fades in over 20 ms: no click. Off, or at 1:1 with Gain 0 dB, the sound passes through untouched.
- No added latency.
- Auto-Master doesn't use Glue yet: it sets Ratio 1:1 and Gain 0 dB, so its loudness plan stays right. Add Glue after Auto-Master; it makes the mix a little quieter, so raise Drive by about the Reduction reading to keep your loudness (check Short-term).

### 08 Dynamics

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
| Impact | −100 to +100 % (0 = off) | Punch of the hits in this band. **+** makes the start of each hit harder (kick thump, snare crack); **−** softens it (spiky hats, a poking snare). The steady sound in between isn't touched and the band's loudness is kept, so you hear punch, not volume. |
| Sustain | −100 to +100 % (0 = off) | Length of the tails in this band. **+** lifts each sound as it dies away (longer, fuller 808s; longer cymbals and reverb); **−** pulls tails down faster (tighter 808s that don't blur into the next note; drier hats). The band's loudness is kept, so the hits trade places with the tails a little. |
| Adaptive release | on | Lets go quickly after short hits, slower on long notes. |
| Channels (pill under the bands) | STEREO, MID ONLY, SIDE ONLY (STEREO) | What every band works on: both channels, only the centre (vocal, kick, 808, snare) or only the stereo edges (wide synths, reverbs, wide hats). The other part passes through untouched. |

How to set a band: raise Ratio to 2:1, then lower Threshold until the band button shows 1–3 dB on the loud hits.

How to use Sustain (808 length): 808s blurring into each other? Set the **Low** band's Sustain to −20 to −40 %. Want longer, fuller 808s? Try +20 to +40 %. Compare with MATCH. At ±100 %, tails move about 3 to 4 dB against the hit, the front of each hit is left alone, and loudness stays within about 1 dB.

How to use Impact: for a kick that doesn't hit, set the **Low** band's Impact to +20 to +40 % and compare with MATCH. For harsh, spiky hats, try the **High** band at −20 to −40 %. Impact and Sustain work even with Ratio at 1:1 (no compression), as long as Dynamics is on.

- At +100 %, hits come out about 2 dB punchier at the same loudness. Small moves go a long way.
- Punchier hits mean taller peaks, so the Maximizer works a little harder. If its GR jumps on every kick, turn Impact down or use the Soft Clipper.
- Loudness keeping settles over about 2 seconds after you move the knob.
- Auto-Master doesn't use Impact or Sustain yet: it sets both to 0 % (off) on every band, so its loudness plan stays right. Add them after Auto-Master, then check Short-term and adjust Drive if needed. The undo arrow brings back your earlier settings.

**Mid only and Side only.** Click the **STEREO** pill under the band buttons to step through STEREO, MID ONLY and SIDE ONLY. It applies to all four bands at once (and to Impact and Sustain).

- **MID ONLY:** compresses only the centre. The width (reverbs, wide synths and hats) is left alone, so glue on the vocal and drums doesn't pull the stereo image in.
- **SIDE ONLY:** compresses only the stereo edges. The vocal, kick and 808 are left alone. Good for wide hats that are too harsh (High band) or a reverb that pumps. The edges are quieter than the full mix, so the same Threshold catches less: set it a few dB lower.
- Changing it while compressing never clicks: the compression fades out and back in (about 40 ms), then each band moves to the new part's level at its Attack and Release speed. Impact and Sustain pause for up to about 0.2 s while they settle on the new part.
- Solo still plays the whole band; Listen plays only what is taken off the centre (or the edges).
- On a one-channel (mono) track the pill has no effect: Dynamics works on the whole sound, as on STEREO.
- Auto-Master always sets STEREO.

### 09 Stereo Imager

Sets the width of lows, mids and highs. Only the stereo (side) part changes; the centre (vocal, kick, 808) keeps its level and its place.

| Control | Range (default) | What it does |
| --- | --- | --- |
| Lows | below 250 Hz, 0–200 % (100) | Keep at 100 %. Wide bass smears and costs headroom. |
| Mids | 250 Hz–5 kHz, 0–200 % (100) | Small moves only. |
| Highs | above 5 kHz, 0–200 % (100) | Hats, reverbs, ad-libs. Try 110–115 %. |
| Correlation meter | −1 to +1 | Mono safety. +1 = mono, 0 = wide, below 0 = out of phase. Keep it above +0.3. |

### 10 Saturation

Adds harmonics so bass translates on small speakers and the master feels denser. Quiet parts pass untouched; the level stays put as you add Drive.

| Control | Range (default) | What it does |
| --- | --- | --- |
| Mode | Tape, Tube, Clip (Tape) | Tape = smooth and warm. Tube = rich, fuller bass. Clip = firm and bright. Changing it while playing fades over in about 0.1 s. |
| Focus | Full, Bass only, Highs only, Bass + Highs (Full) | Full = the whole mix gets harmonics. Bass only = only the low end (below about 150 Hz: 808 and kick). Highs only = only the top (above about 4 kHz: hats, cymbals, air). Bass + Highs = both, with the middle (the vocal) left clean. |
| Channels | Stereo, Mid, Side (Stereo) | Which part of the stereo image gets the harmonics. Stereo = both channels alike. Mid = only the centre (vocal, kick, 808, snare): the wide parts stay clean. Side = only the stereo edges (wide synths, reverbs, wide hats): adds width and air around a clean centre. Works together with Focus (for example Bass only + Mid = only the 808 in the centre). |
| Drive | 0–18 dB (0) | How many harmonics. 0 = off. |
| Mix | 0–100 % (100) | Blend with the clean sound. |
| Harmonics meter | −60 to −10 dB | What was added. Aim for about −25 to −35 dB. |

**Bass only ("808 to phone").** Phone and laptop speakers can't play an 808's low notes, so on them the 808 disappears. They can play its harmonics (the 2nd, 3rd, 4th... overtones, roughly 90–600 Hz), and your ear fills in the low note. With Focus on Bass only, Saturation adds those harmonics to the 808 and kick only: vocals, hats and snares stay clean (even at full Drive), the clean sound itself is never filtered, and no extra sub is added, so the 808 sounds about the same on headphones and big speakers. The Harmonics meter reads about the same as Full at the same Drive. Switching between Full and Bass only is click-free. Auto-Master picks Bass only (with Tube) for Trap / Hip-Hop and R&B, unless the part it heard has almost no low end.

Try it: Bass only, Tube, Drive +9. Play the song on your phone or laptop speakers and flip A/B with MATCH on: on B the 808 should come through. Songs saved before Focus existed open on Full and sound exactly as before.

**Highs only ("sheen").** Saturation adds harmonics only to the top of the mix (above about 4 kHz): dull hats and cymbals get sheen and sparkle, while the vocal's body, the snare and the 808 stay clean. Only the new harmonics are added (the hats themselves keep their exact level), and they are kept above about 3 kHz, so they never muddy the middle. The top is much quieter than the whole mix, so the Harmonics meter reads lower here: about −40 to −50 dB is already a clear sheen. Try Tape, Drive +9 to +12; if the hats turn fizzy, lower Drive or Mix.

**Bass + Highs.** Both at once, each on its own: the 808 gets harmonics for phones and the hats get sheen, while the middle, where the vocal lives, stays clean. The meter reads about like Bass only (the 808's harmonics are the louder part). Try Tube, Drive +9.

**Channels Mid / Side.** Mid keeps the harmonics in the centre, so a wide mix stays exactly as wide and clean at the edges. Side adds harmonics only to the stereo edges: wide synths and reverbs get thicker and the mix sounds a little wider, while the vocal and 808 stay clean. With Side, check MONO now and then: what Side adds disappears in mono (that's normal), but the centre must still sound right. On a mono track Side does nothing. The Harmonics meter reads lower on Side (the edges are quieter than the centre) and a little lower on Mid. Switching Channels is click-free. Auto-Master always sets Stereo.

Switching between all four Focus settings is click-free. Auto-Master picks only Full or Bass only. If you drew automation for Focus in Cubase with an older Masterology, check that lane: with four choices, a lane that used to mean Bass only now reads as Bass + Highs. Saved songs themselves open with the Focus they had.

### 11 Soft Clipper

Shaves the tallest drum peaks before the Maximizer, so the limiter works less. This is how loud modern masters stay punchy. It's off until you turn it on.

| Control | Range (default) | What it does |
| --- | --- | --- |
| Clip Level | −9 to +3 dB (0) | Measured from the Maximizer ceiling. Lower = more peaks shaved. |
| Softness | hard–soft, 0–100 % (50) | A rounder knee sounds like punch, not distortion. |
| Oversampling | 1x, 2x, 4x, 8x (4x) | Cuts down harsh aliasing. 4x is the mastering default; 8x is cleanest. |
| Clipping meter | 0 to −6 dB | Peak reduction. Aim for 1–3 dB on the drums. |

### 12 Maximizer

The final true-peak limiter. It makes the master loud and keeps every peak at or under the ceiling, measured the way streaming services measure true peak (ITU BS.1770, peaks between samples included).

| Control | Range (default) | What it does |
| --- | --- | --- |
| Drive | 0–24 dB (0) | Pushes the mix into the limiter. More Drive = louder. |
| Input Trim | −24–0 dB (0) | Turns your mix down **before the whole chain**. Drive only pushes up, so this is how Masterology masters a mix that already arrives louder than your target. Auto-Master sets it when needed; at 0 dB the mix goes in as it is. A (bypass) ignores it. |
| Ceiling | −3 to 0 dBTP (−1) | The highest peak allowed, between samples too (measured the way streaming services measure true peak). −1 suits most masters; loud ones (−9 LUFS and louder) are safer at −1.5 (what Auto-Master sets for Loud and Club) to −2 (Spotify's advice), because their MP3/AAC/Ogg copies peak higher. |
| Release | 10–1000 ms (120) | How fast the limiter lets go. Shorter = louder but can distort bass. |
| Release Mode | Adaptive (default) or Fixed | Adaptive recovers fast after drum hits and slowly on 808s and pads, so it doesn't pump. |
| Bass First | off–100 % (off) | When the 808 (below 120 Hz) pushes a peak into the limiter, the low end is turned down first (up to 12 dB) instead of the whole mix, so the vocal and hats stop dipping with every 808. |
| Dither | Off, 16-bit, 24-bit (Off) | For exports at a lower bit depth. Set it to the bit depth you export. |
| GR | meter | Gain reduction. Under about 4 dB is comfortable. |

Set Drive while watching **Short-term** in the footer. If Learn says **Diminishing returns**, the extra Drive only adds squashing, not loudness: turn it down.

**Bass First (808-aware limiting).** A normal limiter turns the whole master down when a peak hits the ceiling. In trap the peaks are usually the 808, so the vocal and hats dip on every 808 hit: that's the "pumping" you hear on loud masters. With Bass First up, the low end takes that reduction instead. On a test beat (heavy 808 under a steady vocal) the vocal dipped 3.9 dB per 808 hit with Bass First off and 0.9 dB with it at 100 %, with the same ceiling.

- Try 50–100 % on trap with a heavy 808, and compare with MATCH.
- The trade: on the loudest hits the 808 sits a little lower. Learn shows how much the low end is taking right now.
- Turning it on or off glides in about a quarter of a second, so it never clicks.
- Masterology's delay is 1.5 ms longer than before (Cubase compensates it automatically).
- Auto-Master sets it back to off (it plans the plain limiter); add it afterwards.

**Dither.** When you export a **16-bit** WAV (CD, some distributors), set Dither to 16-bit. It adds a very quiet hiss (about −98 dBFS) so quiet fades and reverb tails don't turn gritty when the export is cut to 16 bits. For a 24-bit or 32-bit float export, leave it Off (or 24-bit for a 24-bit WAV). For it to work, Masterology must be the last thing on Stereo Out and the Stereo Out fader must sit at 0 dB. Don't also turn on Cubase's own dither.

## MATCH and A / B

Louder always sounds better, so compare at equal loudness: turn on **MATCH**, then flip **A / B**.

- **A** plays your original mix (Masterology bypassed). **B** plays the master.
- With MATCH on, play **B for a few seconds** first. Masterology learns how much louder the master is (roughly after half a second, exactly after about 3 seconds), then turns the louder side **down** to match. It never turns anything up, so B can't go past its ceiling.
- After **Auto-Master**, a snapshot you haven't heard yet, or opening the project, MATCH learns the level afresh the same way. After you change settings by hand, it follows within about 20 seconds of B; press **RESET** (under the Output meter) to make it learn afresh at once.
- Quick A / B flips don't disturb it: it only learns from B played for 3 seconds or more at a stretch.
- If your original mix peaks above 0 dBFS, both sides are turned down together so A doesn't clip your audio interface and sound worse than it is.
- Now judge tone and punch, not volume. Ask: is B clearer, fuller, punchier? Did the kick or vocal lose anything?
- On B the meters show the **real master**, never the matched level. On A they show your original mix.
- MATCH switches itself off when you close Masterology's window, when you open the project again, and during a normal (offline) export. For a Realtime Export, switch it off yourself first.

On headphones, compare at a low, comfortable volume and take breaks. Ears tire fast, and tired ears like everything brighter.

### Blind test: compare masters without knowing which is which

MATCH takes care of loudness, but you still know which side is Masterology. To compare Masterology's master with another one (your old chain, another plug-in, a pro master of the same song) fairly, use **Masterology Blind Test** (`MasterologyBlindTest.exe`, in the same download as the plug-in):

1. In Cubase, export each version of the same song with **File > Export > Audio Mixdown** (WAV), for example: your mix with Masterology bypassed (`my mix.wav`), Masterology's master (`masterology.wav`), and the other master (`other.wav`). Two to eight files.
2. Drag all the files at once onto **MasterologyBlindTest.exe** (in the **BlindTest** folder of the download).
3. It measures each file, turns the louder ones **down** to the quietest one's loudness (never up, never into clipping), shuffles them and saves them as **Sample A**, **Sample B**, ... in a new folder next to your files.
4. Listen in any player at one fixed volume. Jump between the samples at the same moment of the song and fill in **Score sheet.txt** (score 1–10 and notes for each).
5. Only then open **Answer key.txt** to see which sample was which.

To compare just one part (say 30 seconds from the hook at 1:00), run it from a command prompt: `MasterologyBlindTest.exe "my mix.wav" masterology.wav --start 60 --length 30` (quotes around names with spaces).

The test also checks the fight is fair and measures each file:

- **Fair fight?** A master pushed harder usually loses some punch, and a higher ceiling gives it more room, so the masters should be **made** at the same loudness (within 0.5 LU) and with the same ceiling, not only played back equally loud. The test matches every master to the file with **masterology** in its name (or **polisher**, the old name) and says how far off each one is ("ozone.wav: needs 0.9 LU more: push it about 0.9 dB harder"). Export that one again and rerun. A file with the word **mix** in its name (`my mix.wav`) is your unmastered mix and is left out of the check. The message is on the score sheet too; it never says which sample is which.
- **Measurements** (at the end of **Answer key.txt**, so they can't sway your ears): loudness, true peak, **Kick** and **Snare** punch (how far the hits stick out in their own band; a squashed master reads lower), loudness range, **Width** and **BassW** (stereo width overall and below 120 Hz), and the tone balance in six bands (Sub to Air) against the average of all samples. They show *why* the winner won: more punch, more 808, a brighter top, a wider image. Punch doesn't hear distortion: your ears judge that.

### Shoot-out: Masterology against other mastering tools

To find out how Masterology really stacks up, run two blind rounds on one of your own songs against **iZotope Ozone 12** (it has a free trial), **TDR Limiter 6 GE** (Tokyo Dawn Labs; check their site for a free or demo version) and **LoudMax** (free, by Thomas Mundt).

Rules for every master, so nothing but the sound decides:

- The same mix, the same export settings (WAV, the project's sample rate, 24-bit).
- **The same ceiling as Masterology's.** Auto-Master sets the Ceiling to −1.5 dBTP on the Loud and Club targets (−1.0 on Stream): set every other tool's ceiling (output) to that number, with its true-peak mode on where it has one. The test warns you when the masters' true peaks differ.
- **The same loudness**, within 0.5 LU. Make Masterology's master first, then match the others to it: the test tells you how many dB to change each one.
- **Name the files the way the test understands:** `masterology.wav` for Masterology's master, `my mix.wav` for your unmastered mix, and any name without "masterology" or "mix" for the others (`ozone.wav`, `tdr.wav`, `loudmax.wav`).
- One tool on Stereo Out at a time; switch the others off (Cubase: the insert's power button).

**Round 1, the whole master: Masterology vs Ozone.**

1. Masterology: run **Auto-Master** (your genre, Loud target), adjust if you like, note the Ceiling, export `masterology.wav`.
2. Ozone 12: run its **Master Assistant** on the same song, set its Maximizer's ceiling to Masterology's, then move its Threshold until it matches `masterology.wav`'s loudness. Export `ozone.wav`.
3. Your mix with nothing on Stereo Out: `my mix.wav`.
4. Drag all three onto MasterologyBlindTest.exe. If it says the fight isn't fair yet, fix that file and run it again. Then listen, score, and open the answer key.

**Round 2, loudness only: Masterology's limiter vs TDR Limiter 6 GE vs LoudMax.** This shows which one gets loud with the least damage.

1. Masterology: a fresh instance (every module is neutral when loaded), Ceiling as in Round 1. Turn on the **Soft Clipper** (Clip Level 0, Softness 50 %), raise **Drive** until the export matches Round 1's loudness. Export `masterology.wav`.
2. The same with the Soft Clipper **off** and Drive raised to the same loudness: `masterology no clipper.wav`. (The others are limiters only, so this shows Masterology's limiter alone.)
3. TDR Limiter 6 GE, its default preset, output ceiling as Masterology's: raise its input (or lower its threshold) to the same loudness. Export `tdr.wav`.
4. LoudMax: Output at Masterology's ceiling, ISP (true peak) detection on if your version has it, lower Threshold to the same loudness. Export `loudmax.wav`.
5. Blind test all four.

Tips: listen at one comfortable volume and take a break between rounds. Run a round twice (each run shuffles again): the same winner both times is a good sign, a different winner each time means the masters are close. Keep the answer keys and score sheets: if another tool wins, the measurements show what to fix in Masterology.

## Undo, redo and snapshots

### Undo and redo

The two arrows at the top step **back and forward through your changes, one at a time**. Point at an arrow to see what it will do ("Undo: Drive 4.5 → 6.5 dB" puts Drive back from 6.5 to 4.5 dB; "Undo: Auto-Master (Trap / Hip-Hop, Balanced)"); after a click, a note at the bottom says what was undone.

- **One step per change**: a knob drag from press to release, a typed value, a switch, a choice, a dragged EQ point (its frequency and gain together). Quick mouse-wheel turns or arrow-key presses on one knob count as one step.
- **Auto-Master** is one step that puts back everything it set (A / B included), Tone Steady's target and the report that was there before. **APPLY** of the Stem EQ suggestion, a **snapshot recall**, and Tone Steady's **LEARN** and **CLEAR** are one step each.
- Undo something and then change something else, and the undone steps are gone (nothing to redo any more). Changes you made after an Auto-Master run are never thrown away by undoing it: they come off first, one by one.
- **Keys**: Ctrl+Z undoes; Ctrl+Shift+Z or Ctrl+Y redoes (Cmd on a Mac). Click inside Masterology's window first, or Cubase gets the keys and uses its own undo. While you type a value into a box, Ctrl+Z works on the text.
- Masterology keeps the **last 100 steps** while its window is closed and reopened. They are **not saved** with the project: opening the project, or loading a preset (anything that replaces all of Masterology's settings at once), starts a new list.
- **Not in the list**: A / B, MATCH, MONO, PHONE, Listen and Solo (listening only), the Settings switches for the meters, and the genre / intensity / target choices. Automation and changes Cubase makes are never in it either, so playing back automation doesn't fill it up.

### Snapshots S1–S4

The four small slots next to the arrows each keep a **whole sound**: every knob and switch in every module, Tone Steady's target (your song's tone, or following the music), plus the Auto-Master report that explains it. They are **not A / B**: A is always your original mix and B the master; S1–S4 are four versions of the master.

| You do | What happens |
| --- | --- |
| Click an **empty** slot (dim) | It keeps the settings you hear now. |
| Click a **stored** slot | Those settings come back smoothly: knobs glide there and switches fade over, so nothing clicks or drops out. One undo step. |
| Click **STORE**, then a slot (or **Shift-click** a slot) | The slot is replaced with what you hear now. |

The **red** slot is the one playing; a dot after it (**S1•**) means you've changed something since you stored or recalled it. Snapshots are saved with your Cubase project. They don't store A / B, MATCH, the listening aids or Auto-Master's genre / intensity / target, and recalling one never changes them.

**A fair compare with MATCH.** The first time you hear a snapshot, MATCH learns how loud it plays compared with your original: roughly after half a second of B, exactly after about 3 seconds. Play it for about **15 seconds** and MATCH **remembers that snapshot's level**: flip back to it later and it is matched at once (MATCH holds that level for 3 seconds while the loudness reading, which looks at the last 3 seconds, catches up). Storing new settings in a slot makes MATCH learn that slot again.

**Try this: Balanced vs Punchy.** Pick **Balanced**, click Auto-Master and play your whole song from the start (it finishes by itself), then click **S1**. Pick **Punchy**, run Auto-Master the same way and click **S2**. Turn on **MATCH**, play your chorus and listen to S1 for 15 seconds, then S2 for 15 seconds. Now flip **S1 / S2** every few seconds: you hear punch and weight, not volume. Keep the one you like by clicking it (the undo arrow can always take you back).

## Listening aids: Listen, Solo, MONO and PHONE

These change only what you **hear**, never your master: they are not saved with the song, they switch off when you close Masterology, and a normal export never contains them. While any of them is on, a **red banner** across the top of the spectrum says what you are hearing ("LISTENING: what Saturation adds — not the master"); **BACK TO MASTER** on the banner turns everything off. The meters keep measuring the real master the whole time.

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

What a module adds or removes is often 20–40 dB quieter than the music, too quiet to hear at your normal volume. So Masterology **turns quiet changes up** (toward 15 dB under the master, at most +24 dB) and says so on the banner ("+18 dB so you can hear it"); Learn tells you how far under the music it really sits ("In your master it sits about 33 dB under the music"). Judge the **character** (ticks or crackle, a ring or a note), and remember the real level. A change that would peak over the ceiling is turned down instead ("kept under the ceiling"); a solo plays at its real level. Each change is lined up with the master to the sample and given the same Drive, Soft Clipper level (a Clip Level below 0 adds level) and limiting it gets in the master. On **A** nothing is processed, so a Listen is silent there.

### MONO and PHONE: check how your master translates

Next to MATCH, two small buttons check your master on other speakers. They work with A / B and MATCH (MATCH still compares fairly: both sides get the same check).

- **MONO** plays left and right mixed together on both speakers, like a club PA or a phone. If a part gets much quieter or disappears (often wide synths, reverbs or doubled vocals), it is out of phase and will vanish on mono speakers: bring the Stereo Imager's Mids and Highs toward 100 %, or narrow that part in the mix.
- **PHONE** plays your master like a phone speaker: mono, thin below about 300 Hz (200 Hz is about 13 dB down and almost nothing under 150 Hz comes through, so no 808 sub), soft above 8 kHz, with a small bump around 2 kHz, at about the same loudness on a balanced mix (a mix whose weight is all sub gets quieter, as on a real phone). **Does the 808 still come through?** You hear its upper harmonics, not its sub; that's normal, and it's what Saturation (Tube is the fullest) and an 808's own distortion are for. If the 808 disappears, add a little Saturation Drive or 1–2 dB of Bass on the Tonal EQ, or saturate the 808 track in the mix.

While any check or Listen is on, nothing leaves Masterology above 0 dBFS. A loud, bright master can go over on PHONE (the 2 kHz bump): then the whole check is turned down a few dB, smoothly rather than peak by peak, so what you hear is your master and not the check distorting. It lets go as soon as you switch the check off.

## Learn and Quick Help

The **Learn** panel on the right explains, live, what the selected module is doing to your track. Click **LEARN ON** at the top to show or hide it.

- **WHAT'S HAPPENING**: what you're hearing right now.
- **WHAT EACH CONTROL IS DOING**: each control's value and its effect on your song.
- **Red boxes** need a decision. Each one says **Do I need to fix it?** and gives numbered **How** steps.
- **Blue boxes** are tips, or warnings that don't need fixing. Nothing blue is a problem.
- After Auto-Master, the top of each module's Learn shows the reasons for its settings, and **WHY IT SOUNDS LIKE THIS** holds the whole report.

**Quick Help** (the **?** button, top right) is a one-page cheat sheet: the chain order and the first moves for every module. Its **OPEN THE FULL MANUAL** button opens this manual inside Masterology, with contents on the left; it works offline. Press **Esc** or **×** to close it.

**Settings** has three switches:

| Setting | Default | What it does |
| --- | --- | --- |
| Reset loudness meter when playback starts | on | Integrated restarts each time you press play. |
| Restart loudness after you change a control | on | Integrated restarts a moment after a change, so it measures what you hear now. |
| Adaptive release (Maximizer) | on | Same as the Maximizer's Release Mode. |

## The spectrum screen

The big screen in the middle has two views. Switch with **SPECTRUM / HISTORY** at its bottom right; Masterology remembers your choice.

### Spectrum

- **White line**: your mix as it arrives. **Red line**: the master after the whole chain. Both are tilted 4.5 dB per octave, so a well-balanced mix reads roughly flat instead of falling to the right.
- **Dotted line** (for example **Trap target**): the tone typical masters in the genre you picked at the top have, the same balance Auto-Master's EQ aims for. It follows the genre, and it is moved to the loudness of what's playing, so you compare tone, not volume.
  - Where your red master line sits **above** the dotted line, your master has more of that range than typical masters in your genre; **below**, less. Differences of 1–2 dB are normal.
  - Below about 60 Hz all the lines get rough: the screen can't draw the lowest notes finely.
  - It shows a steady, average master. Busy drums and hats read up to about 1 dB above it, and wide stereo highs about 1 dB below it (the screen adds left and right together, so the purely stereo part of the sound doesn't count).
- **Dashed yellow line**: a loaded reference's tone (Match ref). Auto-Master then aims for the reference, so the genre line is hidden. Click the genre line's legend entry to show or hide it, with or without a reference.
- **Blue curve**: the Tonal EQ's change, when the EQ is shaping or its page is open.

### Loudness history

**HISTORY** draws the last 2 minutes of playback:

| What you see | What it is |
| --- | --- |
| Red line | The master's Short-term loudness (the last 3 seconds) |
| White line | Your mix as it comes in |
| Dashed line | Your target (or the reference's loudness, when one is loaded) |
| Blue shading from the top | How much the Maximizer turns the loud moments down: the deeper, the harder it works. Past 6 dB it turns orange |
| Dotted vertical line | Where you stopped and started playback again |

How to read it:

- **Verse and chorus.** A chorus or drop usually sits 1–3 dB above the verse, so the red line steps up and down with your song. If the white line steps but the red one stays flat, the Maximizer is squashing the difference: lower Drive 1–2 dB and play the change again.
- **Limiting.** Short dips of 1–3 dB on the hits are normal. Shading that stays orange (more than 6 dB) or dips past 9 dB is heavy limiting, and the kick and snare lose punch (the same advice as the Maximizer's **FIX IT IN THE MIX** in Learn).
- The readout above the graph gives the loud and quiet parts' loudness and the deepest limiting on screen. Click **HISTORY** and Learn reads the graph for you.

Time only runs while your song plays: stopping leaves a gap and the graph waits for you. **RESET** (under the Output meter) clears it; it is not cleared when Integrated restarts at play or after a control change.

## Meters

The footer meters always measure the **master** (what leaves Masterology), using the streaming standard (ITU-R BS.1770 / EBU R128), also while MATCH, a Listen, Solo, MONO or PHONE is on.

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
| Master is too loud or squashed even at Drive 0; input reads far above 0 dBTP | Your old chain (EQ, saturator, compressor, limiter) is still on Stereo Out before Masterology, so the mix arrives already limited and hot | Bypass every other insert on Stereo Out, keep Masterology in the last slot, and run Auto-Master again |
| Input is hot even with nothing else on Stereo Out | The mix itself is too loud | Run Auto-Master: it turns the mix down with Input Trim when it's louder than your target. Or lower Input Trim yourself (Maximizer screen) by about as many dB as the master's Integrated reads above your target |
| Auto-Master says "This doesn't sound like a full mix" | Masterology is on one track, not Stereo Out | Remove it from the track, load it in Stereo Out's last insert slot (F3 > Stereo Out), play the chorus and run Auto-Master again |
| Auto-Master keeps listening | It waits for the song to end (4 seconds without music after at least 30 s of it), or Cycle is on and the loop keeps playing | Let the song play to its end, or click Auto-Master again (or FINISH NOW in Learn) any time after 12 s of music |
| Auto-Master finished while you only paused | After 30 s of music, 4 seconds without music count as the song's end | Fine if it heard the hook. Otherwise run it again and keep the song playing |
| The report says playback jumped | A cycle loop or a playhead move while it listened | Turn Cycle off on the transport bar and play the song once from start to end |
| Auto-Master says it heard too little | Playback stopped or was mostly silence | Click Auto-Master, play the song from the start and let it play (it needs at least 12 s of music) |
| Tone Steady's Target says "following the music" | No song tone learned yet (or CLEAR was pressed, or the song was saved before targets existed) | Press LEARN in Tone Steady (or run Auto-Master) and play the whole song |
| Master lands quieter than the target | Auto-Master protected the drums within the intensity's limit | Fine as it is. To go louder, choose Punchy, or tame the drum peaks in the mix |
| Master isn't as loud as the reference | The reference had its drum peaks controlled in the mix; reaching it would crush yours | Fine as it is (streaming plays both at the same level). To get closer: a clipper on the drum bus, or Punchy |
| Match ref says it can't read the file | Not WAV, AIFF, FLAC or MP3, or a damaged file | Export or convert the song to WAV and load that |
| B sounds better but you're not sure why | B is louder | Turn on MATCH and play B for a few seconds before comparing |
| B sounds louder with MATCH on after you changed settings by hand | MATCH follows hand changes over about 20 s of B | Keep playing B, or press RESET under the Output meter: MATCH learns the level afresh in about 3 s |
| One snapshot sounds louder right after you switch, even with MATCH on | MATCH hasn't heard that snapshot yet | Give it 3 seconds of B; play each snapshot for about 15 s once and MATCH remembers its level, so flips are matched at once |
| Ctrl+Z undoes something in Cubase instead of in Masterology | Cubase had the keyboard | Click inside Masterology's window first, or use the undo arrow at the top |
| The undo arrow is grey after opening the project | The undo list isn't saved with the project | Working as intended. Snapshots are saved: keep versions you may want back in S1–S4 |
| Masterology sounds thin, mono, crackly or like a phone; a red banner is on the spectrum | A listening aid is on (Listen, Solo, MONO or PHONE) | Click BACK TO MASTER on the banner. They switch off by themselves when you close Masterology and are never in a normal export |
| Listen plays silence | The module isn't changing anything right now (or you're on A) | Fine: nothing to hear. Play a louder part, switch to B, or set the module up first |
| Kick or snare sounds weaker than the mix | Too much limiting | Lower Drive 1–2 dB, or turn on the Soft Clipper so it takes some peaks |
| Learn says Diminishing returns | Drive past the point where it adds loudness | Lower Drive until Short-term starts to drop, then back up 0.5 dB |
| Harsh or crunchy top | Saturation, clipping or a big EQ boost | Lower Saturation Drive 3 dB or Mix to 30 %; raise Clip Level or Softness; keep EQ moves to 1–3 dB |
| Bass disappears on a phone or in mono | Stereo bass or too much width | Mono Below at 100 Hz; Imager Lows at 100 %; keep Correlation above +0.3 |
| Unlimiter does nothing | Your mix is open (Squash 12.5 dB or more) | Working as intended: it only restores audio that was squashed |
| Auto-Master left the Unlimiter off on a squashed beat | Your target is about as loud as the beat already is, so the Maximizer would press the hits straight back down | Working as intended. Pick a quieter target (Stream −14) if you want the punch back |
| Integrated keeps restarting | The Settings switches restart it on play and after changes | Turn off those switches in Settings if you want one long reading |
| Masterology isn't in the plug-in list | Cubase hasn't scanned it | Check the VST3 folder, then Studio > VST Plug-in Manager > Update |
| Masterology opens a small screen that says BASIC VIEW | The Microsoft Edge WebView2 Runtime is missing on this PC | Click GET WEBVIEW2, install the runtime, close and reopen Masterology. Audio keeps working meanwhile |

## FAQ

**Which target should I pick?** Stream −14 for the most natural sound on Spotify and YouTube. Loud −9 is common for trap and hip-hop. Club −7 is for DJ and club playback, not streaming.

**Do I have to use Auto-Master?** No. Every module works by hand. Auto-Master is a fast, explained starting point.

**Should I use Match ref or a genre?** A genre is a safe average. A reference is better when you know exactly whose sound you want. Pick a reference in the same style as your song: a trap reference for a trap song.

**Can a Listen, Solo, MONO or PHONE end up in my export?** Not in a normal (offline) export: Masterology switches them off. They are not saved with the song and switch off when you close Masterology. Only a Realtime Export plays what you hear, so check that the red banner is gone first.

**Will Auto-Master change my Stem EQ?** Never. It may suggest a Vocals move with an APPLY button; you decide.

**Do I have to play the whole song for Auto-Master?** It's best: it then finds the loudest part by itself and Tone Steady learns your whole song's tone. If you're in a hurry, play the hook or drop and click Auto-Master again after 12 seconds.

**Will my export sound like what I heard?** Yes. With Tone Steady holding your song to its learned tone (Target: your song), Tone Steady does the same to each part wherever playback or the export starts; the other modules settle within the first second or two.

**Why −1 dBTP for the ceiling?** Streaming services convert to MP3/AAC/Ogg, which pushes peaks up, and the louder the master the more. −1 dBTP leaves enough room for most masters. Auto-Master uses −1.5 for Loud −9 and Club −7: on our test masters at −9 and −7 LUFS the Ogg copies then peaked within about 0.4 dB of 0 dBTP, where −1 let them reach about +1 (MP3 320 kbps copies stayed under 0 either way). Spotify advises −2 dBTP for masters louder than −14 LUFS; set the Ceiling to −2 to follow it exactly (it costs a little loudness). No ceiling makes clipping after conversion impossible, only unlikely.

**Can I trust my headphones?** For tone and punch, yes, if you use MATCH and moderate volume. For loudness, trust the meters. Check on a phone and in a car too.

**Can I put Masterology on a beat or a single track?** On a finished beat, yes (the Unlimiter is made for loud beats). On one instrument or vocal, no: put it on Stereo Out.

**Does Masterology add delay?** A little (Stem EQ about 40 ms). Cubase compensates it automatically, and the export lines up.

**Is my project saved with Masterology's settings?** Yes. All settings, including genre, intensity and target, and your snapshots S1–S4 are saved with the Cubase project. The undo list is not.

**What's the difference between A / B and S1–S4?** A / B compares your original mix (A) with the master (B). S1–S4 are four versions of the master you can switch between, for example two Auto-Master runs.

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
| Snapshot | A stored copy of every setting (S1–S4), to switch between versions of your master. |
