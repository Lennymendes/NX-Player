// NX Player — plug-in « Spectre circulaire » (MIT).
let smooth = [];
let level = 0;

function render(g, w, h, nx, slot) {
  const bands = (nx.audio && nx.audio.spectrum) || [];
  const a = nx.audio || {};
  const accent = (nx.theme && nx.theme.accent) || "#00F5A0";
  const accent2 = (nx.theme && nx.theme.accent2) || "#00D9F5";
  const cx = w / 2, cy = h / 2;
  const rms = Math.max(a.rmsL || 0, a.rmsR || 0);
  level += (rms - level) * 0.2;
  const r0 = Math.min(w, h) * (0.22 + level * 0.12);
  const span = Math.min(w, h) * 0.26;
  const n = bands.length || 64;

  g.color(accent, 0.10 + level * 0.3);
  g.ellipse(cx - r0, cy - r0, r0 * 2, r0 * 2);

  for (let i = 0; i < n; i++) {
    const v = bands[i] || 0;
    smooth[i] = (smooth[i] || 0) + (v - (smooth[i] || 0)) * 0.35;
    const ang = (i / n) * Math.PI * 2 - Math.PI / 2;
    const len = 3 + smooth[i] * span;
    const x1 = cx + Math.cos(ang) * r0, y1 = cy + Math.sin(ang) * r0;
    const x2 = cx + Math.cos(ang) * (r0 + len), y2 = cy + Math.sin(ang) * (r0 + len);
    g.color(i % 2 ? accent : accent2, 0.55 + smooth[i] * 0.45);
    g.line(x1, y1, x2, y2, Math.max(2, (Math.PI * 2 * r0) / n - 2));
  }
  const p = nx.player || {};
  if (p.format) {
    g.color(nx.theme ? nx.theme.text : "#FFFFFF", 0.9);
    g.text(p.format, cx - r0, cy - 9, r0 * 2, 18, "center", 12, true);
  }
}
