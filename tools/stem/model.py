"""The small Stem EQ model. Kept deliberately plain (conv + GRU + dense) so the plug-in can run it
with a few hundred lines of C++ and no ML library."""
import torch, torch.nn as nn
from common import NBANDS, LOOKAHEAD

HIDDEN = 128


class StemNet(nn.Module):
    def __init__(self, nin=NBANDS, bidir=False, hidden=HIDDEN, layers=1):
        """nin: input bands per channel (NBANDS for features(), NIN for features_hi()).  bidir: the GRU
        also runs backwards over the whole excerpt (experiment: how much longer look-ahead helps).
        hidden / layers: GRU size (capacity experiment)."""
        super().__init__()
        # Input normalisation is learned (per feature) so the C++ side just applies scale/shift.
        self.norm = nn.Conv1d(2 * nin, 2 * nin, 1, groups=2 * nin)
        # Looks at frames t-LOOKAHEAD..t+LOOKAHEAD (the plug-in delays the audio by LOOKAHEAD frames).
        self.conv = nn.Conv1d(2 * nin, hidden, 2 * LOOKAHEAD + 1)
        self.gru = nn.GRU(hidden, hidden, layers, batch_first=True, bidirectional=bidir)
        self.out = nn.Linear(hidden * (2 if bidir else 1), 4 * NBANDS)

    @staticmethod
    def load(path):
        sd = torch.load(path)
        layers = sum(k.startswith('gru.weight_ih_l') and not k.endswith('reverse') for k in sd)
        net = StemNet(sd['norm.weight'].shape[0] // 2, 'gru.weight_ih_l0_reverse' in sd,
                      sd['conv.weight'].shape[0], layers)
        net.load_state_dict(sd); net.eval()
        return net

    def forward(self, x):                      # x: (N, 2*nin, T) -> logits (N, 4, NBANDS, T - 2*LOOKAHEAD)
        h = torch.relu(self.conv(self.norm(x)))
        h, _ = self.gru(h.transpose(1, 2))
        y = self.out(h).transpose(1, 2)
        return y.reshape(y.shape[0], 4, NBANDS, -1)
