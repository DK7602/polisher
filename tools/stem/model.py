"""The small Stem EQ model. Kept deliberately plain (conv + GRU + dense) so the plug-in can run it
with a few hundred lines of C++ and no ML library."""
import torch, torch.nn as nn
from common import NBANDS, LOOKAHEAD

HIDDEN = 128


class StemNet(nn.Module):
    def __init__(self):
        super().__init__()
        # Input normalisation is learned (per feature) so the C++ side just applies scale/shift.
        self.norm = nn.Conv1d(2 * NBANDS, 2 * NBANDS, 1, groups=2 * NBANDS)
        # Looks at frames t-LOOKAHEAD..t+LOOKAHEAD (the plug-in delays the audio by LOOKAHEAD frames).
        self.conv = nn.Conv1d(2 * NBANDS, HIDDEN, 2 * LOOKAHEAD + 1)
        self.gru = nn.GRU(HIDDEN, HIDDEN, batch_first=True)
        self.out = nn.Linear(HIDDEN, 4 * NBANDS)

    def forward(self, x):                      # x: (N, 2B, T) -> logits (N, 4, B, T - 2*LOOKAHEAD)
        h = torch.relu(self.conv(self.norm(x)))
        h, _ = self.gru(h.transpose(1, 2))
        y = self.out(h).transpose(1, 2)
        return y.reshape(y.shape[0], 4, NBANDS, -1)
