"""Does Stem EQ hear a trap 808 as Bass?  The held-out MUSDB songs with a synthetic 808 line
(synth808.py, its own eval clips) added to the bass stem, summed as energies like train.py's remixes
(the model input is built the same way for every model, so the comparison is fair).  Scores the
+/-6 dB moves of evaluate.py, plus the Bass move alone (the knob a trap producer turns).
  python evaluate808.py [model.pt ...]     SKIP=n as in evaluate.py; BANK=synth_808_eval_hard / _clean for
                                           808 sounds training never used (synth808.py STYLE)"""
import glob, os, sys, numpy as np, torch
from common import LOOKAHEAD
from evaluate import move_error, shares
from model import StemNet

if __name__ == '__main__':
    nets = [(os.path.basename(m), StemNet.load(m)) for m in sys.argv[1:]]
    bank = np.load(os.path.join('data', 'extra', os.environ.get('BANK', 'synth_808_eval') + '.npz'))
    files = sorted(glob.glob('data/val_*.npz'))[int(os.environ.get('SKIP', 0)):]
    names = [n for n, _ in nets] + ['static', 'oracle']
    allm = {n: [] for n in names}; bassm = {n: [] for n in names}
    for i, f in enumerate(files):
        d = np.load(f)
        m, s = 10 ** d['m'].astype(np.float64), 10 ** d['s'].astype(np.float64)
        F = m.shape[-1]
        reps = int(np.ceil(F / bank['m'].shape[-1]))
        c = i % len(bank['m'])
        m8 = np.tile(10 ** bank['m'][c].astype(np.float64), reps)[:, :F]
        s8 = np.tile(10 ** bank['s'][c].astype(np.float64), reps)[:, :F]
        m[2] += m8; s[2] += s8
        x = np.concatenate([np.log10(m.sum(0) + 1e-9), np.log10(s.sum(0) + 1e-9)]).astype(np.float32)
        E = (2 * (m + s))[..., LOOKAHEAD:-LOOKAHEAD]
        cands = [(n, shares(net, x)) for n, net in nets]
        cands.append(('static', np.broadcast_to(E.sum(-1, keepdims=True) / E.sum((0, -1), keepdims=True), E.shape)))
        cands.append(('oracle', E / E.sum(0, keepdims=True)))
        for n, sh in cands:
            allm[n].append(np.mean([move_error(sh, E, k, db) for k in range(4) for db in (6, -6)]))
            bassm[n].append(np.mean([move_error(sh, E, 2, db) for db in (6, -6)]))
    print(f'{len(files)} held-out songs + a synthetic 808 line on Bass; move error vs ideal, dB (lower is better)')
    for n in names:
        print(f'{n[:22]:22s} all moves {np.mean(allm[n]):7.2f}   Bass +/-6 {np.mean(bassm[n]):7.2f}')
