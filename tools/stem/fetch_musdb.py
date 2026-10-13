"""Download MUSDB18-HQ (HF dataset danjacobellis/musdb18HQ) shard by shard and prep each song as soon as
its four stems are in, deleting the audio as it goes, so the whole set needs only ~1 GB of free disk.
  python fetch_musdb.py <work-dir> [train|validation ...]
Songs land in data/ as <song>.npz (train split) or val_<song>.npz (MUSDB test split; held out).
Licence: MUSDB18 is for non-commercial research only (fine while Masterology is free)."""
import os, shutil, sys, time, urllib.request
import numpy as np, pyarrow.parquet as pq, soundfile as sf
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import SOURCES
from prep import OUT, save

URL = 'https://huggingface.co/datasets/danjacobellis/musdb18HQ/resolve/main/data/{}-{:05d}-of-{:05d}.parquet'
SHARDS = {'train': 41, 'validation': 22}


def run(work, splits):
    os.makedirs(OUT, exist_ok=True); os.makedirs(work, exist_ok=True)
    for split in splits:
        n = SHARDS[split]
        pending = {}                                          # song -> {stem: wav bytes path}
        for i in range(n):
            shard = os.path.join(work, f'{split}-{i}.parquet')
            for attempt in range(5):                          # the CDN sometimes cuts a transfer short
                if os.path.exists(shard): break
                try:
                    urllib.request.urlretrieve(URL.format(split, i, n), shard + '.part'); os.replace(shard + '.part', shard)
                except OSError as err:
                    print('retrying', shard, err, flush=True); time.sleep(2 ** attempt)
            for rb in pq.ParquetFile(shard).iter_batches(batch_size=4):
                for row in rb.to_pylist():
                    stem = row['instrument']
                    if stem not in SOURCES: continue
                    song = os.path.basename(os.path.dirname(row['path'].rstrip('/'))) or row['path']
                    name = ('val_' if split == 'validation' else '') + song.replace(' ', '_')
                    d = os.path.join(work, 'stems', name); os.makedirs(d, exist_ok=True)
                    with open(os.path.join(d, stem + '.wav'), 'wb') as f: f.write(row['audio']['bytes'])
                    pending.setdefault(name, set()).add(stem)
            os.remove(shard)
            for name in [s for s, have in pending.items() if have >= set(SOURCES)]:
                d = os.path.join(work, 'stems', name)
                if not os.path.exists(os.path.join(OUT, name + '.npz')):
                    parts = [sf.read(os.path.join(d, s + '.wav'), dtype='float32', always_2d=True) for s in SOURCES]
                    save(name, np.stack([w.T[:2] for w, _ in parts]), parts[0][1])
                shutil.rmtree(d); del pending[name]
            print(f'{split} shard {i + 1}/{n} done, {len(pending)} songs waiting for stems', flush=True)
        if pending: print('incomplete songs skipped:', sorted(pending))


if __name__ == '__main__':
    run(sys.argv[1], sys.argv[2:] or ['train', 'validation'])
