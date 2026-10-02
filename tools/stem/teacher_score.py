"""python teacher_score.py <dir with val_*/{vocals,drums,bass,other}.wav>
Score the HT-Demucs teacher on MUSDB held-out songs with evaluate.py's move error, using its own
band shares, against the TRUE stems.  This is the realistic ceiling for a share-based Stem EQ."""
import glob, os, sys, numpy as np, torch, soundfile as sf
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import SOURCES, LOOKAHEAD, stem_energies
from evaluate import move_error
from demucs.pretrained import get_model
from demucs.apply import apply_model
torch.set_num_threads(4)
m = get_model('htdemucs_ft'); m.eval()
rows = []
for d in sorted(glob.glob(sys.argv[1] + '/val_*/')):
    st = np.stack([sf.read(os.path.join(d, s + '.wav'), dtype='float32', always_2d=True)[0].T[:2] for s in SOURCES])
    sr = 44100; mix = st.sum(0)
    x = torch.from_numpy(mix); ref = x.mean(0); mu, sd = ref.mean(), ref.std() + 1e-8
    with torch.no_grad(): out = apply_model(m, ((x - mu) / sd)[None], shifts=1, split=True, overlap=0.25)[0] * sd + mu / 4
    dd = dict(zip(m.sources, out.numpy())); est = np.stack([dd[s] for s in SOURCES])
    E = stem_energies(st, sr)[0].astype(np.float64)[..., LOOKAHEAD:-LOOKAHEAD]
    Et = stem_energies(est, sr)[0].astype(np.float64)[..., LOOKAHEAD:-LOOKAHEAD]
    n = min(E.shape[-1], Et.shape[-1]); E, Et = E[..., :n], Et[..., :n]
    share = Et / (Et.sum(0, keepdims=True) + 1e-20)
    r = [move_error(share, E, k, db) for k in range(4) for db in (6, -6)]
    rows.append(r); print(os.path.basename(d.rstrip('/'))[:30].ljust(32), ' '.join('%6.1f' % v for v in r), ' avg %.1f' % np.mean(r), flush=True)
a = np.mean(rows, 0); print('TEACHER avg'.ljust(32), ' '.join('%6.1f' % v for v in a), ' avg %.1f' % a.mean())
