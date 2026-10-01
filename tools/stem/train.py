"""Train StemNet on data/*.npz.  Loss: band-energy-weighted error of the predicted share of each stem,
which is what decides how wrong a Stem EQ move sounds.
Augmentation (the first full run memorised its ~110 songs): most excerpts are remixes built from the
stored per-stem mid / side band energies - random stem levels, stems borrowed from other songs,
sometimes a stem left out.  Stems are summed as energies (no in-band cross terms: vs the real mix that
is off by ~2 dB median in the lowest bands, 0.4 dB mid, 0.15 dB top), so a share of excerpts stays the
real mix, and validation always uses real mixes.  Saves the checkpoint with the best validation loss."""
import glob, os, sys, time, numpy as np, torch
from common import LOOKAHEAD
from model import StemNet

torch.set_num_threads(os.cpu_count())
SEG = 400                                      # frames per training excerpt (~2.3 s)
OWN_SHARE = 0.35                               # share of excerpts from teacher-labelled (own) songs
REMIX = float(os.environ.get('REMIX', 0.75))   # share of excerpts that are remixes
FEAT = os.environ.get('FEAT', 'x')             # model input: x (64 bands) or xh (detailed, no remix yet)
OUT = os.environ.get('OUT', 'stemnet.pt')


def load(files):
    """Per song: input features (x float32 or xh float16), e / m / s (4, B, F) float16 log energies (kept
    small in memory; excerpts are converted as they are drawn), own."""
    out = []
    for f in files:
        d = np.load(f)
        out.append((d[FEAT], d['e'], d['m'], d['s'], bool(d['teach'])))
    return out


def remix(data, i, t, rng, p):
    """Excerpt of song i at frame t, remixed: each stem from this song or (half the time) another,
    level -9..+6 dB, 10 % of stems left out (but never all).  Returns x (2B, SEG), e (4, B, SEG)."""
    B = data[i][1].shape[1]
    Em, Es, E = np.zeros((4, B, SEG)), np.zeros((4, B, SEG)), np.zeros((4, B, SEG))
    keep = rng.random(4) > 0.1
    if not keep.any(): keep[rng.integers(4)] = True
    for k in range(4):
        if not keep[k]: continue
        j, tt = i, t
        if rng.random() < 0.5:
            j = rng.choice(len(data), p=p); tt = rng.integers(0, data[j][0].shape[1] - SEG)
        g = 10 ** (rng.uniform(-9, 6) / 10)
        sl = slice(tt, tt + SEG)
        E[k] = g * 10 ** data[j][1][k, :, sl].astype(np.float64)
        Em[k] = g * 10 ** data[j][2][k, :, sl].astype(np.float64)
        Es[k] = g * 10 ** data[j][3][k, :, sl].astype(np.float64)
    x = np.concatenate([np.log10(Em.sum(0) + 1e-9), np.log10(Es.sum(0) + 1e-9)])
    return x.astype(np.float32), np.log10(E + 1e-9).astype(np.float32)


def song_weights(data):
    """Own (teacher-labelled) songs get OWN_SHARE of the draws, so a few trap songs are not drowned
    out by ~100 MUSDB songs."""
    own = np.array([d[-1] for d in data])
    if own.all() or not own.any(): return np.full(len(data), 1 / len(data))
    return np.where(own, OWN_SHARE / own.sum(), (1 - OWN_SHARE) / (~own).sum())


def batch(data, n, rng, p=None):
    xs, es = [], []
    for _ in range(n):
        i = rng.choice(len(data), p=p)
        t = rng.integers(0, data[i][0].shape[1] - SEG)
        if FEAT == 'x' and rng.random() < REMIX:
            xx, ee = remix(data, i, t, rng, p)
        else:
            xx, ee = data[i][0][:, t:t + SEG].astype(np.float32), data[i][1][:, :, t:t + SEG].astype(np.float32)
        g = rng.uniform(-1.5, 0.5)             # overall level (log energies: an offset)
        xs.append(xx + g); es.append(ee + g)
    return torch.from_numpy(np.stack(xs)), torch.from_numpy(np.stack(es))


def loss_fn(logits, e):
    e = e[..., LOOKAHEAD:-LOOKAHEAD]
    E = 10 ** e                                # 4,B,T stem energies
    total = E.sum(1, keepdim=True)
    target = E / total
    pred = torch.softmax(logits, 1)
    w = total / total.mean(dim=(2, 3), keepdim=True)        # loud bands matter more...
    w = w ** 0.5                                             # ...but not completely
    return (w * (pred - target) ** 2).sum(1).mean()


if __name__ == '__main__':
    files = sorted(glob.glob('data/*.npz'))
    val = [f for f in files if os.path.basename(f).startswith('val_')] or files[:1]
    tr = [f for f in files if f not in val] or files
    dtr, dval = load(tr), load(val)
    net = StemNet(dtr[0][0].shape[0] // 2); opt = torch.optim.AdamW(net.parameters(), 2e-3, weight_decay=1e-4)
    steps = int(sys.argv[1]) if len(sys.argv) > 1 else 2000
    sched = torch.optim.lr_scheduler.OneCycleLR(opt, 2e-3, total_steps=steps)
    p = song_weights(dtr); best = np.inf
    rng = np.random.default_rng(0); t0 = time.time()
    print(FEAT, 'input, remix', REMIX if FEAT == 'x' else 0, ';', len(dtr), 'training songs,', sum(t for *_, t in dtr), 'own;', len(dval), 'validation', flush=True)
    for step in range(1, steps + 1):
        x, e = batch(dtr, 16, rng, p)
        loss = loss_fn(net(x), e)
        opt.zero_grad(); loss.backward(); torch.nn.utils.clip_grad_norm_(net.parameters(), 1.0); opt.step(); sched.step()
        if step % 200 == 0 or step == steps:
            with torch.no_grad():
                vl = [(loss_fn(net(torch.from_numpy(x[None].astype(np.float32))), torch.from_numpy(e[None].astype(np.float32))).item(), t)
                      for x, e, _, _, t in dval]
            msg = ' '.join(f'{k} {np.mean([l for l, t in vl if t == own]):.4f}' for k, own in (('val-musdb', False), ('val-own', True))
                           if any(t == own for _, t in vl))
            v = np.mean([l for l, _ in vl])
            if v < best: best = v; torch.save(net.state_dict(), OUT); msg += ' *'
            print(f'step {step} train {loss.item():.4f} {msg} ({time.time() - t0:.0f}s)', flush=True)
    print('best val', round(best, 4), 'params', sum(p.numel() for p in net.parameters()))
