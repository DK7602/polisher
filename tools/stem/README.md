# Stem EQ model (training tools)

The plug-in's Stem EQ does not rebuild stems. A small model predicts, for every STFT frame and each of
64 ERB-spaced bands, what share of the energy belongs to vocals / drums / bass / other. The user's
per-stem EQ gains are mixed by those shares into one per-bin gain, so with all gains at 0 dB the audio
passes through exactly (delayed). The big HT-Demucs model (MIT) is only the teacher.

Ceiling (teacher's own shares as "perfect" masks, user's song): a +/-6 dB stem move lands within
-18 to -25 dB of the ideal result at 1024/256, 64 bands (bass +6 is the weakest at -18).

Latency: NFFT 1024 + LOOKAHEAD 3 x HOP 256 = 1792 samples (about 40 ms at 44.1 kHz).

Setup (scratch venv): `pip install torch torchaudio demucs soundfile librosa lameenc`

1. `python prep.py teach song1.wav song2.mp3 ...`   (user's songs, teacher-labelled, clean licence)
   MUSDB18-HQ (non-commercial research licence): download parquet shards from HF dataset
   `danjacobellis/musdb18HQ`, then `python musdb.py stems/ shard*.parquet` and
   `python prep.py stems stems/train/<song dir> ...` (test-split songs come out as `val_*`).
   Teacher-labelled songs put the 808 under Bass: below ~120 Hz, low hits lasting 0.3 s or more, sound
   ringing on under the kicks, and any vocals/other become bass; short low hits stay drums (kick).
   Prefix a file with `val_` to hold it out for validation.
   Faster for all of MUSDB: `python fetch_musdb.py <work-dir>` streams the 63 parquet shards, preps each song
   as soon as its stems are in and deletes the audio (needs ~1 GB of scratch; data/ ends up ~13 GB; run ONE
   at a time: two preps at once ran out of memory). 100 train songs + 50 held-out (val_*) songs.
   `python synth808.py 200 30` -> data/extra/synth_808.npz (synthetic trap 808 lines for training) and
   `python synth808.py 40 30 909 synth_808_eval` (separate clips for evaluate808.py).
2. `python train.py 20000` -> stemnet.pt. Options (environment): AUG=1 per-stem EQ / pitch / tempo / width
   augmentation in the remixes, SYN808=0.3 adds an 808 to the bass stem of that share of remixes, EMA=0.999
   weight averaging, W_POW share-loss band weighting, SEG / BATCH excerpt length and count, VAL_SONGS=10 picks
   checkpoints by the move error on the first 10 val songs, THREADS (leave a core free: 4 threads on 4 busy
   cores slowed a step from 0.1 s to 7 s).
3. `SKIP=10 python evaluate.py a.pt b.pt ...` (+/-6 dB move error vs ideal on the other 40 held-out songs,
   with the static and oracle rows and how often each model beats static), `SKIP=10 python evaluate808.py ...`
   (the same songs with a synthetic 808 line on Bass), `python render.py ...` (listening clips).
4. `python export.py [model.pt]` -> dsp/src/StemNetWeights.h + tests/StemNetReference.h; the C++ test
   checks dsp/src/StemEQ.cpp against PyTorch frame by frame (shares within 1e-5).

Keep `common.py` and the C++ in lock-step: band layout, window, frame timing, features.

## Model v3 (2026-10-10, shipped in v0.14.1)

Same network as v2 (conv + 1-layer GRU, 128 hidden, 247k params, 64-band input; the C++ engine is
unchanged), new training: all of MUSDB18-HQ (100 songs; the user's own songs were not available this
session), AUG=1, SYN808=0.35 with SYN_BANK=synth_808_mix (standard + distorted/octave + clean sub 808s),
EMA=0.999, 12k steps, checkpoint picked by the move error on the first 10 held-out songs
(`AUG=1 SYN808=0.35 SYN_BANK=synth_808_mix EMA=0.999 VAL_SONGS=10 python train.py 12000`).

Move error on the other 40 held-out MUSDB songs (dB, lower is better; all / Bass +/-6 for the 808 sets):

| test | v2 | v3 | static (best fixed EQ) | oracle |
| --- | --- | --- | --- | --- |
| plain songs | -14.28 | -14.25 | -13.88 | -18.23 |
| + standard 808s | -18.43 / -14.81 | -20.41 / -18.12 | -19.89 / -17.39 | -23.26 / -20.20 |
| + distorted 808s | -17.90 / -13.94 | -20.78 / -17.71 | -20.26 / -16.86 | -23.10 / -19.55 |
| + clean sub 808s | -15.38 / -11.27 | -17.96 / -16.06 | -17.55 / -15.58 | -21.37 / -18.96 |

v3 beats static on 80 % of the plain songs (v2: 70 %) and on every 808 set; v2 lost to static whenever an
808 was present. Caveat: the 808 sets are synthetic (styles training used, other clips and seeds).
Vocal level readings: v3 - v2 = -0.07 dB mean (Auto-Master's genre vocal norms stay valid), 0.80 dB from
the true level vs v2's 0.94. Tried and not kept: 9 s excerpts (slower to learn), blending the output with
its own running average (worse), share-loss weighting by band energy (no gain).
