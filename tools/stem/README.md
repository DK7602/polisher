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
2. `python train.py 20000` -> stemnet.pt
3. `python evaluate.py models/stemnet_v2.pt` (+/-6 dB move error vs ideal), `python render.py ...` (listening clips).
4. `python export.py [model.pt]` -> dsp/src/StemNetWeights.h + tests/StemNetReference.h; the C++ test
   checks dsp/src/StemEQ.cpp against PyTorch frame by frame (shares within 1e-5).

Keep `common.py` and the C++ in lock-step: band layout, window, frame timing, features.
