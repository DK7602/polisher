"""Synthetic trap 808s, so a model trained on MUSDB (which has almost no trap) still hears 808s as Bass,
the way the user wants them (prep.low_end_to_bass does the same for teacher-labelled songs).
Each clip is a mono 808 line on a 130-150 BPM grid: notes E1-B1 (and the octave above now and then),
a fast pitch drop at the start, 0.15-1.6 s decays, gentle tanh saturation for the harmonics that make
an 808 audible on small speakers, notes cut by the next one (as in a real 808 track).
  python synth808.py [clips] [seconds] [seed] [name]  -> data/extra/<name>.npz (default synth_808) (m, s: log mid / side energy, (N, B, F));
  kept out of data/ itself, where every .npz is a song
Train with SYN808=<share of remixes that get an 808 added to the bass stem>."""
import os, sys, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import stem_energies

SR = 44100


def line(rng, seconds):
    n = int(seconds * SR)
    bpm = rng.uniform(130, 150)
    step = 60.0 / bpm / 2                                       # eighth notes
    out = np.zeros(n)
    t = rng.uniform(0, 4) * step
    drive = rng.uniform(1.0, 4.0)
    while t < seconds:
        f0 = 41.2 * 2 ** (rng.integers(0, 8) / 12) * (2 if rng.random() < 0.15 else 1)
        length = step * rng.choice([1, 2, 3, 4, 6, 8])
        decay = rng.uniform(0.15, 1.6)
        i0, i1 = int(t * SR), min(n, int((t + length) * SR))
        tt = np.arange(i1 - i0) / SR
        f = f0 * (1 + rng.uniform(0.5, 2.0) * np.exp(-tt / 0.012))     # the drop at the start
        ph = 2 * np.pi * np.cumsum(f) / SR
        env = np.exp(-tt / decay) * np.minimum(1, tt / 0.002)
        env[-min(len(env), 64):] *= np.linspace(1, 0, min(len(env), 64))   # cut by the next note, no click
        out[i0:i1] = np.tanh(drive * env * np.sin(ph)) / np.tanh(drive)
        t += length + step * rng.choice([0, 0, 1, 2])
    return out * 0.5


if __name__ == '__main__':
    clips = int(sys.argv[1]) if len(sys.argv) > 1 else 200
    seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 30.0
    rng = np.random.default_rng(int(sys.argv[3]) if len(sys.argv) > 3 else 808)
    name = sys.argv[4] if len(sys.argv) > 4 else 'synth_808'
    M, S = [], []
    for c in range(clips):
        x = line(rng, seconds).astype(np.float32)
        stems = np.stack([np.stack([x, x])])                    # one mono stem, (1, 2, T)
        _, em, es = stem_energies(stems, SR)
        M.append(np.log10(em[0] + 1e-9).astype(np.float16)); S.append(np.log10(es[0] + 1e-9).astype(np.float16))
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'data', 'extra', name + '.npz')
    os.makedirs(os.path.dirname(out), exist_ok=True)
    np.savez_compressed(out, m=np.stack(M), s=np.stack(S))
    print(out, np.stack(M).shape)
