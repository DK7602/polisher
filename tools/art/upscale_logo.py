"""High-definition Masterology logo.

The header logo (plugin/ui/assets/logo.webp) was cut out of the approved mock-up at 840 x 265 and was soft: the
mock-up's compression smeared the crown, the waves and the chrome letters. This rebuilds it with Real-ESRGAN (x4plus),
which redraws the fine detail, and writes it at twice the old size (1680 x 530), so it stays sharp in the largest
window on a high-DPI screen. The colour runs premultiplied (over black) and the transparency runs as its own picture,
so the glow keeps its soft edge and no dark fringe appears around the letters.

Needs only numpy and Pillow (the network runs in numpy; no PyTorch): a few minutes on a laptop CPU.
    curl -L -o RealESRGAN_x4plus.pth https://github.com/xinntao/Real-ESRGAN/releases/download/v0.1.0/RealESRGAN_x4plus.pth
    python3 tools/art/upscale_logo.py tools/art/logo_source.webp RealESRGAN_x4plus.pth plugin/ui/assets/logo.webp
(tools/art/logo_source.webp is the original 840 x 265 cut-out, kept so this can be run again.)
"""

import pickle
import sys
import time
import zipfile

import numpy as np
from PIL import Image

SCALE_OUT = 2   # the written logo: 2x the source (the network makes 4x, then a clean 2:1 downsample)


# ---- reading a PyTorch checkpoint without PyTorch ---------------------------------------------------------------
def load_pth (path):
    z = zipfile.ZipFile (path)
    root = z.namelist()[0].split ("/")[0]
    dtypes = { "FloatStorage": np.float32, "HalfStorage": np.float16, "DoubleStorage": np.float64 }

    class Storage:
        def __init__ (s, key, kind): s.key, s.kind = key, kind

    def rebuild (storage, offset, size, stride, *rest):
        data = np.frombuffer (z.read (f"{root}/data/{storage.key}"), dtypes[storage.kind])
        itemsize = data.itemsize
        return np.lib.stride_tricks.as_strided (data[offset:], shape=tuple (size),
                                                strides=tuple (s * itemsize for s in stride)).astype (np.float32)

    class U (pickle.Unpickler):
        def find_class (s, module, name):
            if name == "_rebuild_tensor_v2": return rebuild
            if name.endswith ("Storage"): return name
            if module == "collections" and name == "OrderedDict":
                import collections; return collections.OrderedDict
            raise pickle.UnpicklingError (f"unexpected {module}.{name}")
        def persistent_load (s, pid):
            _, kind, key, *_ = pid
            return Storage (key, kind)

    state = U (z.open (f"{root}/data.pkl")).load()
    return state.get ("params_ema", state)


# ---- Real-ESRGAN's generator (RRDBNet, 23 blocks), as published by xinntao/Real-ESRGAN, in numpy -----------------
def conv (W, x):
    """3x3 convolution, padding 1. x: (C, H, W) -> (O, H, W)."""
    w, b = W
    c, h, wd = x.shape
    p = np.pad (x, ((0, 0), (1, 1), (1, 1)))
    cols = np.empty ((c, 9, h, wd), np.float32)
    for ky in range (3):
        for kx in range (3):
            cols[:, ky * 3 + kx] = p[:, ky:ky + h, kx:kx + wd]
    out = w.reshape (w.shape[0], -1) @ cols.reshape (c * 9, h * wd)
    out += b[:, None]
    return out.reshape (w.shape[0], h, wd)

def lrelu (x): return np.maximum (x, 0.2 * x)

def up2 (x): return x.repeat (2, axis=1).repeat (2, axis=2)

def net (P, x):
    k = lambda n: (P[n + ".weight"], P[n + ".bias"])
    f = conv (k ("conv_first"), x)
    body = f
    for i in range (23):
        rrdb_in = body
        for r in range (1, 4):
            pre = f"body.{i}.rdb{r}."
            feats = [body]
            for j in range (1, 5):
                feats.append (lrelu (conv (k (pre + f"conv{j}"), np.concatenate (feats, 0))))
            body = conv (k (pre + "conv5"), np.concatenate (feats, 0)) * 0.2 + body
        body = body * 0.2 + rrdb_in
    f = f + conv (k ("conv_body"), body)
    f = lrelu (conv (k ("conv_up1"), up2 (f)))
    f = lrelu (conv (k ("conv_up2"), up2 (f)))
    return conv (k ("conv_last"), lrelu (conv (k ("conv_hr"), f)))


def upscale (P, img, tile=64, pad=10):
    """img: H x W x 3 (0..1) -> 4H x 4W x 3, in overlapping tiles."""
    h, w, _ = img.shape
    x = img.transpose (2, 0, 1).astype (np.float32)
    out = np.zeros ((3, h * 4, w * 4), np.float32)
    tiles = [(y0, x0) for y0 in range (0, h, tile) for x0 in range (0, w, tile)]
    t0 = time.time()
    for n, (y0, x0) in enumerate (tiles):
        y1, x1 = min (y0 + tile, h), min (x0 + tile, w)
        py0, px0, py1, px1 = max (y0 - pad, 0), max (x0 - pad, 0), min (y1 + pad, h), min (x1 + pad, w)
        part = x[:, py0:py1, px0:px1]
        if part.max() <= 0.0:   # empty (transparent) corner: nothing to draw
            continue
        o = np.clip (net (P, part), 0, 1)
        oy, ox = (y0 - py0) * 4, (x0 - px0) * 4
        out[:, y0 * 4:y1 * 4, x0 * 4:x1 * 4] = o[:, oy:oy + (y1 - y0) * 4, ox:ox + (x1 - x0) * 4]
        print (f"  tile {n + 1}/{len (tiles)}  {time.time() - t0:.0f} s", flush=True)
    return out.transpose (1, 2, 0)


def down (arr, size):
    """Lanczos downsample of a float picture (H x W x C, 0..1) to size (w, h), per channel at full precision."""
    chans = [np.asarray (Image.fromarray (np.ascontiguousarray (arr[:, :, c], np.float32), "F").resize (size, Image.LANCZOS))
             for c in range (arr.shape[2])]
    return np.clip (np.stack (chans, axis=2), 0.0, 1.0)


if __name__ == "__main__":
    src, weights, dst = sys.argv[1], sys.argv[2], sys.argv[3]
    P = load_pth (weights)

    rgba = np.asarray (Image.open (src).convert ("RGBA")).astype (np.float32) / 255.0
    h, w0 = rgba.shape[:2]
    alpha = rgba[:, :, 3:4]
    premul = rgba[:, :, :3] * alpha                        # the colour as it shows over black
    print ("colour ..."); C = upscale (P, premul)
    print ("alpha ...");  A = upscale (P, np.repeat (alpha, 3, axis=2)).mean (axis=2, keepdims=True)
    A = np.maximum (A, C.max (axis=2, keepdims=True))      # never less cover than the colour it carries

    size = (w0 * SCALE_OUT, h * SCALE_OUT)
    C, A = down (C, size), down (A, size)
    A[A < 1.5 / 255.0] = 0.0                               # the network's faint haze in the empty corners
    rgb = np.where (A > 0, np.clip (C / np.maximum (A, 1e-6), 0, 1), 0)
    out = np.concatenate ([rgb, A], axis=2)
    Image.fromarray ((out * 255 + 0.5).astype (np.uint8), "RGBA").save (dst, quality=94, alpha_quality=100, method=6)
    print ("wrote", dst, size)
