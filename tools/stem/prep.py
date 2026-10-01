"""Turn songs into training examples: model input features and the target share of each stem per
band and frame.  Two kinds of input:
  python prep.py stems <dir-with-vocals/drums/bass/other.wav>... -> real stems (MUSDB-style)
  python prep.py teach <song.wav|mp3>...                       -> stems from the HT-Demucs teacher
                                                                  (TEACHER=htdemucs for the faster one)
Writes one .npz per song into data/."""
import os, sys, numpy as np, soundfile as sf
from common import SOURCES, features, band_energies

OUT = os.path.join(os.path.dirname(__file__), 'data')


def save(name, stems, sr):
    """stems: (4, 2, T) float32 in SOURCES order."""
    mix = stems.sum(0)
    E = band_energies(stems, sr)                           # 4, B, F
    np.savez_compressed(os.path.join(OUT, name + '.npz'), x=features(mix, sr),
                        e=np.log10(E + 1e-9).astype(np.float16), sr=sr)
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
        return np.stack([d[s] for s in SOURCES]).astype(np.float32), m.samplerate
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
        save(name, stems, sr)
