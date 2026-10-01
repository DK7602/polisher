"""Shared definitions for the Stem EQ model (must match dsp/src/StemEQ.cpp exactly)."""
import numpy as np

SOURCES = ['vocals', 'drums', 'bass', 'other']
NFFT, HOP = 1024, 256          # at 44.1/48 kHz; doubled at 88.2/96 kHz so bands stay in Hz
NBANDS = 64
LOOKAHEAD = 3                  # frames the model may see ahead (adds 3 * HOP latency)
FMIN = 20.0


def band_centres(sr):
    erb = lambda f: 21.4 * np.log10(1 + 0.00437 * f)
    c = np.linspace(erb(FMIN), erb(min(sr / 2, 20000.0)), NBANDS)
    return (10 ** (c / 21.4) - 1) / 0.00437


def band_weights(sr, nfft=NFFT):
    """Triangular ERB-spaced bands over the STFT bins; columns sum to 1, so per-band gains
    interpolate smoothly into per-bin gains and all-ones stays exactly one."""
    f = np.arange(nfft // 2 + 1) * sr / nfft
    cf = band_centres(sr)
    W = np.zeros((NBANDS, len(f)))
    for i in range(NBANDS):
        if i == 0:
            W[i] = f <= cf[0]
        else:
            W[i] = np.clip((f - cf[i - 1]) / (cf[i] - cf[i - 1]), 0, 1) * (f <= cf[i])
        if i == NBANDS - 1:
            W[i] += f > cf[i]
        else:
            W[i] += np.clip((cf[i + 1] - f) / (cf[i + 1] - cf[i]), 0, 1) * (f > cf[i])
    return (W / W.sum(0, keepdims=True)).astype(np.float32)


def stft(ch, sr):
    """ch: (T,) -> complex spectrum (F, frames), and NFFT used. Hann window, centred frames are NOT
    used: frame k covers samples [k*HOP, k*HOP+NFFT), as in the plug-in."""
    n = NFFT if sr < 80000 else 2 * NFFT
    h = HOP if sr < 80000 else 2 * HOP
    w = np.hanning(n + 1)[:-1].astype(np.float32)
    frames = (len(ch) - n) // h + 1
    idx = np.arange(n)[None, :] + h * np.arange(frames)[:, None]
    return np.fft.rfft(ch[idx] * w, axis=1).T, n


def stft_power(x, sr):
    """x: (channels, T) -> power summed over channels, (F, frames)."""
    P = 0
    for ch in x:
        X, n = stft(ch, sr)
        P = P + np.abs(X) ** 2
    return P.astype(np.float32), n


def stem_energies(stems, sr):
    """stems: (S, 2, T) -> band energies (S, B, frames) of L+R power, of mid and of side, from one
    STFT per channel (same numbers as band_energies on stems, on their mid and on their side)."""
    E, Em, Es = [], [], []
    for s in stems:
        L, n = stft(s[0], sr); R, _ = stft(s[1], sr)
        W = band_weights(sr, n)
        E.append(W @ (np.abs(L) ** 2 + np.abs(R) ** 2).astype(np.float32))
        Em.append(W @ (np.abs(0.5 * (L + R)) ** 2).astype(np.float32))
        Es.append(W @ (np.abs(0.5 * (L - R)) ** 2).astype(np.float32))
    return np.stack(E), np.stack(Em), np.stack(Es)


def band_energies(stems, sr):
    """stems: (S, channels, T) -> (S, NBANDS, frames)"""
    out = []
    for s in stems:
        P, n = stft_power(s, sr)
        out.append(band_weights(sr, n) @ P)
    return np.stack(out)


def features(mix, sr):
    """Model input per frame: log band energy of mid and of side (2 x NBANDS)."""
    mid = (mix[0] + mix[1])[None] * 0.5
    side = (mix[0] - mix[1])[None] * 0.5
    em = band_energies(mid[None], sr)[0]
    es = band_energies(side[None], sr)[0]
    return np.concatenate([np.log10(em + 1e-9), np.log10(es + 1e-9)]).astype(np.float32)
