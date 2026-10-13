"""Unpack MUSDB18-HQ parquet shards (HF dataset danjacobellis/musdb18HQ) into the folder layout
prep.py expects:  <out>/<split>/<song>/{vocals,drums,bass,other}.wav
One parquet row is one stem file of one song, and a song's rows may straddle two shards, so prep
only the songs whose four stems are all present.
  python musdb.py <out-dir> shard1.parquet shard2.parquet ...
Licence: MUSDB18 is for non-commercial research only (fine while Masterology is free)."""
import os, sys, pyarrow.parquet as pq

STEMS = {'vocals', 'drums', 'bass', 'other'}

if __name__ == '__main__':
    out, shards = sys.argv[1], sys.argv[2:]
    for shard in shards:
        split = 'val' if os.path.basename(shard).startswith('validation') else 'train'
        pf = pq.ParquetFile(shard)
        for rb in pf.iter_batches(batch_size=4):
            for row in rb.to_pylist():
                stem = row['instrument']
                if stem not in STEMS: continue                      # skip the mixture
                song = os.path.basename(os.path.dirname(row['path'].rstrip('/'))) or row['path']
                name = ('val_' if split == 'val' else '') + song.replace(' ', '_')   # val_ = held out by train.py
                d = os.path.join(out, split, name)
                os.makedirs(d, exist_ok=True)
                with open(os.path.join(d, stem + '.wav'), 'wb') as f:
                    f.write(row['audio']['bytes'])
        print('unpacked', shard, flush=True)
    for split in ('train', 'val'):
        root = os.path.join(out, split)
        if not os.path.isdir(root): continue
        songs = sorted(os.listdir(root))
        full = [s for s in songs if STEMS <= {os.path.splitext(f)[0] for f in os.listdir(os.path.join(root, s))}]
        print(split, len(full), 'complete songs of', len(songs))
