// NX Player — plug-in d'interface « Spectre » (MIT).
// API : render(g, w, h, nx, slot) appelé à chaque image. Voir docs/PLUGINS.md.

const peaks = [];
let last = 0;

function init(nx) {
  nx.console.log("Spectre prêt");
}

function render(g, w, h, nx, slot) {
  const bands = (nx.audio && nx.audio.spectrum) || [];
  const n = bands.length || 64;
  const accent = (nx.theme && nx.theme.accent) || "#00F5A0";
  const accent2 = (nx.theme && nx.theme.accent2) || "#00D9F5";
  const dt = Math.min(0.1, Math.max(0.0, (nx.time || 0) - last));
  last = nx.time || 0;

  // Compact slots (player bar, sidebar) use fewer, wider bars.
  const compact = h < 60 || slot === "playerbar.right";
  const step = compact ? 4 : 1;
  const count = Math.floor(n / step);
  const gap = compact ? 2 : 3;
  const bw = Math.max(1, (w - gap * (count - 1)) / count);

  g.gradient(0, h, accent, 0, 0, accent2);
  for (let i = 0; i < count; i++) {
    let v = 0;
    for (let k = 0; k < step; k++) v = Math.max(v, bands[i * step + k] || 0);
    const bh = Math.max(2, v * (h - 4));
    const x = i * (bw + gap);
    g.roundRect(x, h - bh, bw, bh, Math.min(3, bw / 2));
    peaks[i] = Math.max(v, (peaks[i] || 0) - dt * 0.6);
  }

  if (!compact) {
    g.color(nx.theme ? nx.theme.text : "#FFFFFF", 0.85);
    for (let i = 0; i < count; i++) {
      const y = h - 4 - peaks[i] * (h - 6);
      g.rect(i * (bw + gap), y, bw, 2);
    }
    g.color(nx.theme ? nx.theme.textDim : "#888888", 0.8);
    const label = nx.audio && nx.audio.dsd ? "Densité DSD" : "20 Hz – 20 kHz";
    g.text(label, 0, 0, w, 14, "right", 10, false);
  }
}
