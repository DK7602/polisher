"""Train StemNet on data/*.npz.  Loss: band-energy-weighted error of the predicted share of each stem,
which is what decides how wrong a Stem EQ move sounds.
Augmentation (the first full run memorised its ~110 songs): most excerpts are remixes built from the
stored per-stem mid / side band energies - random stem levels, stems borrowed from other songs,
sometimes a stem left out.  Stems are summed as energies (no in-band cross terms: vs the real mix that
is off by ~2 dB median in the lowest bands, 0.4 dB mid, 0.15 dB top), so a share of excerpts stays the
real mix, and validation always uses real mixes.  Saves the checkpoint with the best validation loss.
AUG=1 (2026-10-10, after the ~110-song memorisation limit): every remixed stem also gets its own random
EQ curve, a small pitch / formant shift (bands moved by up to one band), a tempo change (0.85-1.15x)
and a stereo width change, and 5 % of remixes are folded to mono, so the model can't learn the ~100
songs' stems by heart.  SEG / BATCH set the excerpt length and count (longer excerpts teach the GRU to
use song-level context), EMA keeps an exponential average of the weights, and VAL_SONGS > 0 picks the
checkpoint by the stem-move error (evaluate.py's metric) on that many held-out songs instead of the loss."""
import glob, os, sys, time, numpy as np, torch
from common import LOOKAHEAD
from model import StemNet

torch.set_num_threads(int(os.environ.get('THREADS', os.cpu_count())))   # leave one core free if anything else runs
SEG = int(os.environ.get('SEG', 400))          # frames per training excerpt (400 = ~2.3 s)
BATCH = int(os.environ.get('BATCH', 16))
AUG = os.environ.get('AUG') == '1'
EMA = float(os.environ.get('EMA', 0))          # e.g. 0.999; 0 = off
VAL_SONGS = int(os.environ.get('VAL_SONGS', 0))
W_POW = float(os.environ.get('W_POW', 0.5))     # share-loss weighting by band energy (see share_loss)
SYN808 = float(os.environ.get('SYN808', 0))     # share of remixes that get a synthetic 808 added to the bass stem
SYN = None                                     # the 808 bank (synth808.py; SYN_BANK picks the file), loaded when SYN808 > 0
OWN_SHARE = 0.35                               # share of excerpts from teacher-labelled (own) songs
REMIX = float(os.environ.get('REMIX', 0.75))   # share of excerpts that are remixes
FEAT = os.environ.get('FEAT', 'x')             # model input: x (64 bands) or xh (detailed, no remix yet)
OUT = os.environ.get('OUT', 'stemnet.pt')
# LOSS=move trains on the stem-move error itself (see move_loss); default: share error.


def load(files):
    """Per song: input features x (float16), m / s (4, B, F) float16 log mid / side energy per stem, own.
    The stems' L+R energy is not kept: it is exactly 2 (mid + side), see stem_e (saves ~40 % memory)."""
    out = []
    for f in files:
        d = np.load(f)
        out.append((d[FEAT].astype(np.float16), d['m'], d['s'], bool(d['teach'])))
    return out


def stem_e(song, sl=slice(None)):
    """Log L+R energy per stem, (4, B, frames) float32, from the stored mid / side energies."""
    m, s = song[1][..., sl].astype(np.float64), song[2][..., sl].astype(np.float64)
    return np.log10(2 * (10 ** m + 10 ** s) + 1e-9).astype(np.float32)


def stem_slice(A, t, n, stretch):
    """A: (B, F) log energies -> energies (B, n) from frame t, played stretch x faster (linear interp)."""
    if stretch == 1.0:
        return 10 ** A[:, t:t + n].astype(np.float64)
    pos = t + np.arange(n) * stretch
    i0 = np.minimum(pos.astype(int), A.shape[1] - 2); f = pos - i0
    return (1 - f) * 10 ** A[:, i0].astype(np.float64) + f * 10 ** A[:, i0 + 1].astype(np.float64)


def band_shift(X, u):
    """Move a (B, n) energy spectrum up by u bands (fraction allowed; edges held)."""
    b = np.clip(np.arange(X.shape[0]) - u, 0, X.shape[0] - 1)
    i0 = np.minimum(b.astype(int), X.shape[0] - 2); f = (b - i0)[:, None]
    return (1 - f) * X[i0] + f * X[i0 + 1]


def eq_curve(B, rng):
    """Random smooth EQ (energy factors per band): a tilt up to +/-4 dB at the ends and one bump of
    +/-4 dB, 3-12 bands wide."""
    x = np.arange(B)
    db = rng.uniform(-4, 4) * (2 * x / (B - 1) - 1)
    db += rng.uniform(-4, 4) * np.exp(-0.5 * ((x - rng.uniform(0, B - 1)) / rng.uniform(3, 12)) ** 2)
    return 10 ** (db / 10)[:, None]


def remix(data, i, t, rng, p):
    """Excerpt of song i at frame t, remixed: each stem from this song or (half the time) another,
    level -9..+6 dB, 10 % of stems left out (but never all).  With AUG each stem also gets its own EQ,
    band shift, tempo and width, and 5 % of remixes are mono.  E = 2 (mid + side) energy holds exactly
    (L^2 + R^2 = 2 M^2 + 2 S^2), so E is rebuilt from mid / side.
    Returns x (2B, SEG), e (4, B, SEG)."""
    B = data[i][1].shape[1]
    Em, Es = np.zeros((4, B, SEG)), np.zeros((4, B, SEG))
    keep = rng.random(4) > 0.1
    if not keep.any(): keep[rng.integers(4)] = True
    mono = AUG and rng.random() < 0.05
    for k in range(4):
        if not keep[k]: continue
        j, tt = i, t
        stretch = rng.uniform(0.85, 1.15) if AUG and rng.random() < 0.8 else 1.0
        need = int(np.ceil(SEG * stretch)) + 2
        if rng.random() < 0.5 or data[j][0].shape[1] - tt < need:
            j = rng.choice(len(data), p=p)
        if j != i or data[j][0].shape[1] - tt < need:
            tt = rng.integers(0, data[j][0].shape[1] - need)
        g = 10 ** (rng.uniform(-9, 6) / 10)
        m, sd = stem_slice(data[j][1][k], tt, SEG, stretch), stem_slice(data[j][2][k], tt, SEG, stretch)
        if AUG and rng.random() < 0.8:
            u = rng.uniform(-1, 1); c = eq_curve(B, rng)
            m, sd = c * band_shift(m, u), c * band_shift(sd, u)
            sd = sd * 10 ** (rng.uniform(-6, 3) / 10)
        Em[k] = g * m
        Es[k] = 0.0 if mono else g * sd
    if SYN is not None and rng.random() < SYN808:
        # A trap 808 line under the song, credited to Bass (as the user wants 808s: prep.low_end_to_bass).
        c = rng.integers(len(SYN[0])); tt = rng.integers(0, SYN[0].shape[2] - SEG)
        g = 10 ** (rng.uniform(-6, 6) / 10)
        Em[2] += g * 10 ** SYN[0][c][:, tt:tt + SEG].astype(np.float64)
        Es[2] += 0.0 if mono else g * 10 ** SYN[1][c][:, tt:tt + SEG].astype(np.float64)
    x = np.concatenate([np.log10(Em.sum(0) + 1e-9), np.log10(Es.sum(0) + 1e-9)])
    return x.astype(np.float32), np.log10(2 * (Em + Es) + 1e-9).astype(np.float32)


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
            xx, ee = data[i][0][:, t:t + SEG].astype(np.float32), stem_e(data[i], slice(t, t + SEG))
        g = rng.uniform(-1.5, 0.5)             # overall level (log energies: an offset)
        xs.append(xx + g); es.append(ee + g)
    return torch.from_numpy(np.stack(xs)), torch.from_numpy(np.stack(es))


def share_loss(logits, e):
    """Band-energy-weighted (square-rooted) error of each stem's predicted share."""
    e = e[..., LOOKAHEAD:-LOOKAHEAD]
    E = 10 ** e                                # 4,B,T stem energies
    total = E.sum(1, keepdim=True)
    target = E / total
    pred = torch.softmax(logits, 1)
    w = total / total.mean(dim=(2, 3), keepdim=True)        # loud bands matter more...
    w = w ** W_POW                                           # ...but not completely (0.5; 1 = as the move error weighs them)
    return (w * (pred - target) ** 2).sum(1).mean()


MOVES = torch.tensor([[1 if k == j else 0 for k in range(4)] for j in range(4)], dtype=torch.float32)


def move_loss(logits, e, n_random=4):
    """What evaluate.py measures (and roughly what is heard): for stem moves g, the error energy
    sum_k (g_k - G)^2 E_k against the ideal sum_k g_k^2 E_k, where G = sum_k share_k g_k is the
    plug-in's gain.  Moves: each stem alone +/-6 dB, plus random -6..+6 dB on every stem.
    Returns the mean error in dB (lower is better)."""
    E = (10 ** e[..., LOOKAHEAD:-LOOKAHEAD]).double()         # N,4,B,T
    p = torch.softmax(logits, 1).double()
    db = torch.cat([6 * MOVES, -6 * MOVES, 12 * torch.rand(n_random, 4) - 6]).double()
    g = 10 ** (db / 20)                                      # M,4 amplitude gains
    G = torch.einsum('mk,nkbt->nmbt', g, p)                  # plug-in gain per move
    err = (((g[None, :, :, None, None] - G[:, :, None]) ** 2) * E[:, None]).sum((2, 3, 4))
    ideal = ((g[None, :, :, None, None] ** 2) * E[:, None]).sum((2, 3, 4))
    return (10 * torch.log10(err / ideal + 1e-12)).mean().float()


loss_fn = move_loss if os.environ.get('LOSS') == 'move' else share_loss


def val_move_error(net, dval):
    """evaluate.py's metric (mean over songs of the +/-6 dB move error per stem, dB) on whole songs."""
    from evaluate import move_error
    out = []
    with torch.no_grad():
        for song in dval:
            x = song[0]
            E = 10 ** stem_e(song).astype(np.float64)[..., LOOKAHEAD:-LOOKAHEAD]
            sh = torch.softmax(net(torch.from_numpy(x[None].astype(np.float32))), 1)[0].numpy().astype(np.float64)
            out.append(np.mean([move_error(sh, E, k, db) for k in range(4) for db in (6, -6)]))
    return float(np.mean(out))


if __name__ == '__main__':
    files = sorted(glob.glob('data/*.npz'))
    val = [f for f in files if os.path.basename(f).startswith('val_')] or files[:1]
    tr = [f for f in files if f not in val] or files
    if VAL_SONGS: val = val[:VAL_SONGS]                # the rest of val_* stays unseen for evaluate.py
    dtr, dval = load(tr), load(val)
    if SYN808 > 0:
        bank = np.load(os.path.join('data', 'extra', os.environ.get('SYN_BANK', 'synth_808') + '.npz'))
        SYN = (bank['m'], bank['s'])
    net = StemNet(dtr[0][0].shape[0] // 2, os.environ.get('BIDIR') == '1',
                  int(os.environ.get('HIDDEN', 128)), int(os.environ.get('LAYERS', 1))); opt = torch.optim.AdamW(net.parameters(), 2e-3, weight_decay=1e-4)
    ema = None
    if EMA:
        import copy
        ema = copy.deepcopy(net)
    steps = int(sys.argv[1]) if len(sys.argv) > 1 else 2000
    every = int(os.environ.get('EVAL_EVERY', 500 if VAL_SONGS else 200))
    sched = torch.optim.lr_scheduler.OneCycleLR(opt, 2e-3, total_steps=steps)
    p = song_weights(dtr); best = np.inf
    rng = np.random.default_rng(int(os.environ.get('SEED', 0))); torch.manual_seed(int(os.environ.get('SEED', 0))); t0 = time.time()
    print(FEAT, 'input, remix', REMIX if FEAT == 'x' else 0, 'aug', AUG, 'seg', SEG, 'batch', BATCH, 'ema', EMA, ';', len(dtr), 'training songs,',
          sum(t for *_, t in dtr), 'own;', len(dval), 'validation', flush=True)
    for step in range(1, steps + 1):
        x, e = batch(dtr, BATCH, rng, p)
        loss = loss_fn(net(x), e)
        opt.zero_grad(); loss.backward(); torch.nn.utils.clip_grad_norm_(net.parameters(), 1.0); opt.step(); sched.step()
        if ema is not None:
            with torch.no_grad():
                for pe, pn in zip(ema.parameters(), net.parameters()): pe.mul_(EMA).add_(pn, alpha=1 - EMA)
        if step % every == 0 or step == steps:
            judged = ema if ema is not None else net
            if VAL_SONGS:
                v = val_move_error(judged, dval); msg = f'val-move {v:.3f}'
            else:
                with torch.no_grad():
                    vl = [(loss_fn(judged(torch.from_numpy(song[0][None].astype(np.float32))), torch.from_numpy(stem_e(song)[None])).item(), song[-1])
                          for song in dval]
                msg = ' '.join(f'{k} {np.mean([l for l, t in vl if t == own]):.4f}' for k, own in (('val-musdb', False), ('val-own', True))
                               if any(t == own for _, t in vl))
                v = np.mean([l for l, _ in vl])
            if v < best: best = v; torch.save(judged.state_dict(), OUT); msg += ' *'
            print(f'step {step} train {loss.item():.4f} {msg} ({time.time() - t0:.0f}s)', flush=True)
    print('best val', round(best, 4), 'params', sum(p.numel() for p in net.parameters()))
