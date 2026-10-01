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
   `python prep.py stems musdb/train/<song dir> ...` (MUSDB18-HQ: non-commercial research licence)
   Prefix a file with `val_` to hold it out for validation.
2. `python train.py 20000` -> stemnet.pt
3. (next) export weights for dsp/src/StemEQ.cpp and check C++ against PyTorch frame by frame.

Keep `common.py` and the C++ in lock-step: band layout, window, frame timing, features.
