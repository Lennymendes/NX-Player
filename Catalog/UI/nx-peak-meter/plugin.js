// NX Player — plug-in « Crêtes-mètre numérique » (MIT).
const hold = [0, 0], holdT = [0, 0];
const floorDb = -60;

function db(v) { return v > 1e-6 ? 20 * Math.log10(v) : -120; }
function pos(v) { return Math.max(0, Math.min(1, (db(v) - floorDb) / -floorDb)); }

function render(g, w, h, nx, slot) {
  const a = nx.audio || {};
  const t = nx.time || 0;
  const accent = (nx.theme && nx.theme.accent) || "#00F5A0";
  const accent2 = (nx.theme && nx.theme.accent2) || "#00D9F5";
  const dim = (nx.theme && nx.theme.textDim) || "#888888";
  const peaks = [a.peakL || 0, a.peakR || 0], rms = [a.rmsL || 0, a.rmsR || 0];
  const compact = h < 50;
  const label = compact ? 0 : 14;
  const barH = Math.max(3, (h - label - 6) / 2 - 2);

  for (let c = 0; c < 2; c++) {
    if (peaks[c] >= hold[c] || t - holdT[c] > 1.5) { hold[c] = peaks[c]; holdT[c] = t; }
    const y = 2 + c * (barH + 4);
    g.color(dim, 0.18);
    g.roundRect(0, y, w, barH, barH / 2);
    g.gradient(0, 0, accent, w, 0, accent2);
    g.roundRect(0, y, Math.max(barH, pos(peaks[c]) * w), barH, barH / 2);
    g.color("#FFFFFF", 0.35);
    g.roundRect(0, y + barH * 0.3, pos(rms[c]) * w, barH * 0.4, barH * 0.2);
    const hx = pos(hold[c]) * w;
    g.color(hold[c] >= 0.999 ? "#FF4D5E" : "#FFFFFF", 0.9);
    g.rect(Math.min(w - 2, hx), y, 2, barH);
  }
  if (!compact) {
    g.color(dim, 0.85);
    for (const d of [-48, -24, -12, -6, 0]) {
      const x = ((d - floorDb) / -floorDb) * w;
      g.text(String(d), x - 14, h - 13, 28, 12, "center", 9, false);
    }
  }
}
