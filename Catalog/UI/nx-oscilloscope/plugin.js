// NX Player — plug-in « Oscilloscope » (MIT). Voir docs/PLUGINS.md.
const trails = [];

function render(g, w, h, nx, slot) {
  const s = (nx.audio && nx.audio.scope) || [];
  const accent = (nx.theme && nx.theme.accent) || "#00F5A0";
  const accent2 = (nx.theme && nx.theme.accent2) || "#00D9F5";
  const mid = h / 2;

  // grid
  g.color(nx.theme ? nx.theme.textDim : "#888888", 0.18);
  g.line(0, mid, w, mid, 1);
  if (h > 60) for (let i = 1; i < 4; i++) g.line((w * i) / 4, 4, (w * i) / 4, h - 4, 1);

  // trigger: first rising zero crossing in the first half
  let start = 0;
  for (let i = 1; i < s.length / 2; i++) if (s[i - 1] < 0 && s[i] >= 0) { start = i; break; }
  const n = Math.max(2, Math.floor(s.length / 2));
  const pts = [];
  for (let i = 0; i < n; i++) {
    const v = s[start + i] || 0;
    pts.push((i / (n - 1)) * w, mid - Math.max(-1, Math.min(1, v * 1.6)) * (mid - 3));
  }

  trails.unshift(pts);
  if (trails.length > 4) trails.pop();
  for (let k = trails.length - 1; k >= 1; k--) {
    g.color(accent2, 0.12 / k);
    g.path(trails[k], false, "stroke", 2);
  }
  g.gradient(0, 0, accent, w, 0, accent2);
  g.path(pts, false, "stroke", 2);
}
