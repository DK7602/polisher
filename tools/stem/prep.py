"""Turn songs into training examples: model input features and the target share of each stem per
band and frame.  Two kinds of input:
  python prep.py stems <dir-with-vocals/drums/bass/other.wav>... -> real stems (MUSDB-style)
  python prep.py teach <song.wav|mp3>...                       -> stems from the HT-Demucs teacher
                                                                  (TEACHER=htdemucs for the faster one;
                                                                  KEEP_STEMS=<dir> also saves its stems)
  python prep.py taught <dir-with-saved-teacher-stems>...       -> re-prep saved teacher stems
Writes one .npz per song into data/: x (mix features), xh (detailed mix features), e (log energy per stem), and m / s (log
mid / side energy per stem) so train.py can remix stems for augmentation."""
import os, sys, numpy as np, soundfile as sf
from common import SOURCES, HOP, features, features_hi, stem_energies, band_centres

OUT = os.path.join(os.path.dirname(__file__), 'data')


def long_low_hits(D, fps, min_len=0.3, drop_db=15.0, win=0.5):
    """D: low drum energy per frame.  Frames within drop_db of the local peak form one hit; hits that
    last min_len seconds or more are 808 notes, not kicks (trap kicks here are 0.10-0.14 s)."""
    from scipy.ndimage import maximum_filter1d, label
    pk = maximum_filter1d(D, int(win * fps) | 1)
    lab, n = label(D > pk * 10 ** (-drop_db / 10))
    if not n: return np.zeros(len(D), bool)
    lens = np.bincount(lab)[1:] / fps
    return np.isin(lab, 1 + np.flatnonzero(lens >= min_len))


def low_end_to_bass(E, sr, *also, lo=120.0, hi=200.0, hold=0.15):
    """Teacher labels only (trap; on live rock drums it would steal ~14 % of the kick).  The user wants
    808s under the Bass knob, but HT-Demucs scatters them over drums / other / vocals.  Below `lo` Hz
    (fading out by `hi`): vocals and other become bass (MUSDB's real stems hold almost nothing there);
    from drums, whole low hits lasting 0.3 s or more (808 notes) and anything ringing on under the
    kicks for `hold` seconds become bass.  Short hits stay drums (the kick).  Checked by ear on the
    user's songs: Bubba's 808 at 0:33 moves fully, its kicks and Addie (no 808) do not move.
    E: (4, B, F) energies in SOURCES order; arrays in `also` (e.g. mid / side energies of the same
    stems) get the same moves, as fractions decided on E.  Returns E (and the `also` arrays)."""
    from scipy.ndimage import grey_opening
    v, d, b, o = range(4)
    cf = band_centres(sr)
    w = np.clip((hi - cf) / (hi - lo), 0, 1)[:, None]
    fps = sr / HOP
    n = max(2, int(round(hold * fps)))
    ring = np.minimum(grey_opening(E[d], size=(1, n)), E[d])
    notes = long_low_hits(E[d, cf < lo].sum(0), fps)
    frac = np.where(notes[None, :], 1.0, ring / np.maximum(E[d], 1e-30))    # share of drums moved
    out = []
    for X in (E,) + also:
        X = X.copy()
        take = frac * X[d]
        X[b] += w * (X[v] + X[o] + take)
        X[v] *= 1 - w; X[o] *= 1 - w; X[d] -= w * take
        out.append(X)
    return out[0] if not also else tuple(out)


def save(name, stems, sr, teach=False):
    """stems: (4, 2, T) float32 in SOURCES order."""
    mix = stems.sum(0)
    E, Em, Es = stem_energies(stems, sr)                    # each 4, B, F
    if teach: E, Em, Es = low_end_to_bass(E, sr, Em, Es)
    lg = lambda A: np.log10(A + 1e-9).astype(np.float16)
    path = os.path.join(OUT, name + '.npz')
    with open(path + '.part', 'wb') as fh:                  # atomic: a stopped run leaves no bad .npz
        np.savez_compressed(fh, x=features(mix, sr), xh=features_hi(mix, sr).astype(np.float16), e=lg(E), m=lg(Em), s=lg(Es), sr=sr, teach=teach)
    os.replace(path + '.part', path)
    print(name, E.shape[-1], 'frames')


def teacher():
    import torch
    from demucs.pretrained import get_model
    from demucs.apply import apply_model
    torch.set_num_threads(os.cpu_count())
    # htdemucs_ft (four fine-tuned models, ~4x slower) bleeds less drum into 'other' than htdemucs.
    m = get_model(os.environ.get('TEACHER', 'htdemucs_ft')); m.eval()
    def run(path):
        import librosa
        wav, sr = librosa.load(path, sr=m.samplerate, mono=False)
        if wav.ndim == 1: wav = np.stack([wav, wav])
        x = torch.from_numpy(wav[:2].astype(np.float32))
        ref = x.mean(0); mu, sd = ref.mean(), ref.std() + 1e-8
        with torch.no_grad():
            out = apply_model(m, ((x - mu) / sd)[None], shifts=1, split=True, overlap=0.25)[0] * sd + mu / 4
        # Teacher order is drums, bass, other, vocals; the parts sum back to the song.
        d = dict(zip(m.sources, out.numpy()))
        stems = np.stack([d[s] for s in SOURCES]).astype(np.float32)
        if os.environ.get('KEEP_STEMS'):
            keep = os.path.join(os.environ['KEEP_STEMS'], os.path.splitext(os.path.basename(path))[0])
            os.makedirs(keep, exist_ok=True)
            for s, st in zip(SOURCES, stems): sf.write(os.path.join(keep, s + '.wav'), st.T, m.samplerate, subtype='FLOAT')
        return stems, m.samplerate
    return run


if __name__ == '__main__':
    os.makedirs(OUT, exist_ok=True)
    mode, paths = sys.argv[1], sys.argv[2:]
    run = teacher() if mode == 'teach' else None
    for p in paths:
        name = os.path.splitext(os.path.basename(p.rstrip('/')))[0]
        if os.path.exists(os.path.join(OUT, name + '.npz')): continue
        if mode == 'teach':
            stems, sr = run(p)
        else:
            parts = [sf.read(os.path.join(p, s + '.wav'), dtype='float32', always_2d=True) for s in SOURCES]
            sr = parts[0][1]
            stems = np.stack([w.T[:2] for w, _ in parts])
        save(name, stems, sr, teach=mode in ('teach', 'taught'))
