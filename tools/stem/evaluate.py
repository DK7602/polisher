"""How close does a Stem EQ move get to the ideal?  For each held-out song (data/val_*.npz) and each
stem moved +/-6 dB, compare the plug-in's result (one gain per band and frame = shares mixed by the
stem gains) with the ideal (each stem's own gain).  Error energy relative to the ideal output, in dB:
lower is better.  'oracle' uses the true shares, i.e. the best this band layout can do.
  python evaluate.py [stemnet.pt]"""
import glob, sys, numpy as np, torch
from common import SOURCES, LOOKAHEAD
from model import StemNet


def move_error(share, E, k, db):
    """share, E: (4, B, T).  Stems are treated as uncorrelated within a band."""
    g = np.ones(4); g[k] = 10 ** (db / 20)
    G = np.tensordot(g, share, 1)                                  # plug-in gain per band, frame
    err = (((g[:, None, None] - G) ** 2) * E).sum()
    ideal = ((g[:, None, None] ** 2) * E).sum()
    return 10 * np.log10(err / ideal + 1e-12)


if __name__ == '__main__':
    net = StemNet.load(sys.argv[1] if len(sys.argv) > 1 else 'stemnet.pt')
    feat = 'x' if net.norm.weight.shape[0] == 128 else "xh"
    print(f'{"song":34s} {"":7s}' + ''.join(f'{s + " " + d:>12s}' for s in SOURCES for d in ('+6', '-6')))
    for f in sorted(glob.glob('data/val_*.npz')):
        d = np.load(f)
        E = 10 ** d['e'].astype(np.float64)[..., LOOKAHEAD:-LOOKAHEAD]
        with torch.no_grad():
            p = torch.softmax(net(torch.from_numpy(d[feat].astype(np.float32)[None])), 1)[0].numpy().astype(np.float64)
        q = E / E.sum(0, keepdims=True)
        for name, sh in (('model', p), ('oracle', q)):
            row = [move_error(sh, E, k, db) for k in range(4) for db in (6, -6)]
            print(f'{f[9:-4][:34]:34s} {name:7s}' + ''.join(f'{v:12.1f}' for v in row))
