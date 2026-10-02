"""Render a Stem EQ move to audio, for listening tests.
  python render.py <stem dir> <model.pt> <stem> <dB> <start s> <seconds> <out prefix>
Writes <out prefix>_ideal / _teacher / _model / _original .wav:
  ideal    the separated stems remixed with the move (the target)
  teacher  Stem EQ using the stems' own band shares (best case for a share-based EQ)
  model    Stem EQ using the model's shares (what the plug-in would do)
Stem EQ here = one gain per band and frame, interpolated to bins with band_weights, applied in the
plug-in's STFT (Hann, NFFT / HOP), overlap-added with a Hann synthesis window."""
import os, sys, numpy as np, soundfile as sf, torch
from common import SOURCES, NFFT, HOP, LOOKAHEAD, stft, band_weights, stem_energies, features
from model import StemNet


def apply_band_gains(x, sr, Gb):
    """x: (2, T); Gb: (NBANDS, frames) gain per band and frame -> processed (2, T)."""
    W = band_weights(sr)                                   # B, bins; columns sum to 1
    Gbin = W.T @ Gb                                        # bins, frames
    w = np.hanning(NFFT + 1)[:-1]
    out = np.zeros_like(x, dtype=np.float64)
    for c in range(2):
        X, _ = stft(x[c], sr)
        frames = min(X.shape[1], Gbin.shape[1])
        Y = np.fft.irfft(X[:, :frames] * Gbin[:, :frames], NFFT, axis=0) * w[:, None]
        for k in range(frames):
            out[c, k * HOP:k * HOP + NFFT] += Y[:, k]
    return out / 1.5                                       # Hann^2 at hop NFFT/4 sums to 1.5


if __name__ == '__main__':
    sdir, mpath, stem, db, start, dur, prefix = sys.argv[1:8]
    db, start, dur = float(db), float(start), float(dur)
    parts = [sf.read(os.path.join(sdir, s + '.wav'), dtype='float32', always_2d=True) for s in SOURCES]
    sr = parts[0][1]
    pre = 5.0                                              # model warm-up before the clip
    a, b = int((start - pre) * sr), int((start + dur) * sr)
    st = np.stack([p.T[:2, a:b] for p, _ in parts]).astype(np.float64)
    mix = st.sum(0)
    g = np.ones(4); g[SOURCES.index(stem)] = 10 ** (db / 20)

    E = stem_energies(st.astype(np.float32), sr)[0].astype(np.float64)
    q = E / (E.sum(0, keepdims=True) + 1e-20)              # stems' own shares, (4, B, frames)
    net = StemNet.load(mpath)
    with torch.no_grad():
        p = torch.softmax(net(torch.from_numpy(features(mix.astype(np.float32), sr)[None])), 1)[0].numpy()
    p = np.concatenate([np.repeat(p[:, :, :1], LOOKAHEAD, 2), p, np.repeat(p[:, :, -1:], LOOKAHEAD, 2)], 2)

    outs = {'original': mix, 'ideal': np.tensordot(g, st, 1),
            'teacher': apply_band_gains(mix, sr, np.tensordot(g, q, 1)),
            'model': apply_band_gains(mix, sr, np.tensordot(g, p.astype(np.float64), 1))}
    cut = slice(int(pre * sr), int(pre * sr) + int(dur * sr))
    for k, y in outs.items():
        sf.write(f'{prefix}_{k}.wav', y[:, cut].T.astype(np.float32), sr)
    # How far each is from the ideal over the clip (error energy vs ideal, dB).
    ref = outs['ideal'][:, cut]
    for k in ('original', 'teacher', 'model'):
        e = outs[k][:, cut] - ref
        print(f'{k:9s} error vs ideal {10 * np.log10((e ** 2).sum() / (ref ** 2).sum()):6.1f} dB')
