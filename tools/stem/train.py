"""Train StemNet on data/*.npz.  Loss: band-energy-weighted error of the predicted share of each stem,
which is what decides how wrong a Stem EQ move sounds."""
import glob, os, sys, time, numpy as np, torch
from common import LOOKAHEAD
from model import StemNet

torch.set_num_threads(os.cpu_count())
SEG = 400                                      # frames per training excerpt (~2.3 s)


def load(files):
    out = []
    for f in files:
        d = np.load(f)
        e = d['e'].astype(np.float32)
        out.append((d['x'], e))
    return out


def batch(data, n, rng):
    xs, es = [], []
    for _ in range(n):
        x, e = data[rng.integers(len(data))]
        t = rng.integers(0, x.shape[1] - SEG)
        xx, ee = x[:, t:t + SEG].copy(), e[:, :, t:t + SEG].copy()
        # Augment: overall level, and random per-stem level changes would need the mix rebuilt,
        # so only level here (the features are log energies: level is an offset).
        g = rng.uniform(-1.5, 0.5)
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
    net = StemNet(); opt = torch.optim.AdamW(net.parameters(), 2e-3, weight_decay=1e-4)
    steps = int(sys.argv[1]) if len(sys.argv) > 1 else 2000
    sched = torch.optim.lr_scheduler.OneCycleLR(opt, 2e-3, total_steps=steps)
    rng = np.random.default_rng(0); t0 = time.time()
    for step in range(1, steps + 1):
        x, e = batch(dtr, 16, rng)
        loss = loss_fn(net(x), e)
        opt.zero_grad(); loss.backward(); torch.nn.utils.clip_grad_norm_(net.parameters(), 1.0); opt.step(); sched.step()
        if step % 200 == 0 or step == steps:
            with torch.no_grad():
                vl = np.mean([loss_fn(net(torch.from_numpy(x[None])), torch.from_numpy(e[None])).item() for x, e in dval])
            print(f'step {step} train {loss.item():.4f} val {vl:.4f} ({time.time() - t0:.0f}s)', flush=True)
    torch.save(net.state_dict(), 'stemnet.pt')
    print('params', sum(p.numel() for p in net.parameters()))
