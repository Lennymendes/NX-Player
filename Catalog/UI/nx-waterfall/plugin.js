// NX Player — plug-in « Spectrogramme » (MIT).
const rows = [];
const maxRows = 48;

function mix(a, b, t) { return a + (b - a) * t; }
function hex(c) { const v = parseInt(c.slice(1), 16); return [(v >> 16) & 255, (v >> 8) & 255, v & 255]; }

function render(g, w, h, nx, slot) {
  const bands = (nx.audio && nx.audio.spectrum) || [];
  const n = bands.length || 64;
  const a1 = hex((nx.theme && nx.theme.accent) || "#00F5A0");
  const a2 = hex((nx.theme && nx.theme.accent2) || "#00D9F5");
  rows.unshift(bands.slice(0, n));
  if (rows.length > maxRows) rows.pop();

  const cw = w / n, rh = h / maxRows;
  for (let r = 0; r < rows.length; r++) {
    const row = rows[r];
    for (let i = 0; i < n; i++) {
      const v = row[i] || 0;
      if (v < 0.04) continue;
      const t = i / (n - 1);
      g.color([mix(a1[0], a2[0], t) / 255, mix(a1[1], a2[1], t) / 255, mix(a1[2], a2[2], t) / 255, Math.min(1, v * 1.3) * (1 - r / maxRows)]);
      g.rect(i * cw, r * rh, cw + 0.5, rh + 0.5);
    }
  }
}
