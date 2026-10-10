"""How close does a Stem EQ move get to the ideal?  For each held-out song (data/val_*.npz) and each
stem moved +/-6 dB, compare the plug-in's result (one gain per band and frame = shares mixed by the
stem gains) with the ideal (each stem's own gain).  Error energy relative to the ideal output, in dB:
lower is better.  'oracle' uses the true shares, i.e. the best this band layout can do; 'static' is
the best fixed EQ for each song (its true song-average shares, energy-weighted: for every move that
is the static gain with the least error), the bar a model must beat to be worth running live.
  python evaluate.py [model.pt ...]        SKIP=n leaves out the first n val songs (train.py's
                                           VAL_SONGS picked checkpoints on them), DETAIL=1 per song"""
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


def shares(net, x):
    with torch.no_grad():
        return torch.softmax(net(torch.from_numpy(x.astype(np.float32)[None])), 1)[0].numpy().astype(np.float64)


if __name__ == '__main__':
    import os
    paths = sys.argv[1:] or ['stemnet.pt']
    nets = [(os.path.basename(m), StemNet.load(m)) for m in paths]
    files = sorted(glob.glob('data/val_*.npz'))[int(os.environ.get('SKIP', 0)):]
    detail = os.environ.get('DETAIL') == '1'
    names = [n for n, _ in nets] + ['static', 'oracle']
    rows = {n: [] for n in names}
    for f in files:
        d = np.load(f)
        E = 10 ** d['e'].astype(np.float64)[..., LOOKAHEAD:-LOOKAHEAD]
        cands = [(n, shares(net, d['x' if net.norm.weight.shape[0] == 128 else 'xh'])) for n, net in nets]
        cands.append(('static', np.broadcast_to(E.sum(-1, keepdims=True) / E.sum((0, -1), keepdims=True), E.shape)))
        cands.append(('oracle', E / E.sum(0, keepdims=True)))
        for n, sh in cands:
            rows[n].append([move_error(sh, E, k, db) for k in range(4) for db in (6, -6)])
            if detail: print(f'{os.path.basename(f)[4:-4][:34]:34s} {n[:16]:16s} {np.mean(rows[n][-1]):7.2f}')
    print(f'{len(files)} held-out songs; move error vs ideal, dB (lower is better)')
    print(f'{"":18s}' + ''.join(f'{s + " " + d:>11s}' for s in SOURCES for d in ('+6', '-6')) + f'{"avg":>8s}{"vs static":>11s}')
    st = np.mean(rows['static'])
    for n in names:
        a = np.array(rows[n])
        win = np.mean(a.mean(1) < np.array(rows['static']).mean(1)) * 100
        print(f'{n[:18]:18s}' + ''.join(f'{v:11.2f}' for v in a.mean(0)) + f'{a.mean():8.2f}{a.mean() - st:+11.2f}' +
              (f'   beats static on {win:.0f} % of songs' if n not in ('static', 'oracle') else ''))
