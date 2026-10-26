// NX Player — plug-in « Horloge & session d'écoute » (MIT).
let listened = 0, last = 0, tracks = 0, lastTitle = "";

function two(n) { return (n < 10 ? "0" : "") + n; }
function dur(s) { const m = Math.floor(s / 60); return m >= 60 ? Math.floor(m / 60) + " h " + two(m % 60) : m + " min"; }

function render(g, w, h, nx, slot) {
  const p = nx.player || {};
  const t = nx.time || 0;
  const dt = last > 0 ? Math.min(1, t - last) : 0;
  last = t;
  if (p.playing) listened += dt;
  if (p.title && p.title !== lastTitle) { lastTitle = p.title; tracks++; }

  const accent = (nx.theme && nx.theme.accent) || "#00F5A0";
  const text = (nx.theme && nx.theme.text) || "#FFFFFF";
  const dim = (nx.theme && nx.theme.textDim) || "#888888";
  const d = new Date();
  const ring = Math.min(h - 8, 64);

  // progress ring
  const cx = ring / 2 + 4, cy = h / 2;
  g.color(dim, 0.25);
  g.arc(cx, cy, ring / 2 - 3, 0, Math.PI * 2, 4);
  const prog = p.duration > 0 ? Math.min(1, (p.position || 0) / p.duration) : 0;
  if (prog > 0) { g.color(accent, 1); g.arc(cx, cy, ring / 2 - 3, 0, prog * Math.PI * 2, 4); }
  g.color(text, 1);
  g.text(two(d.getHours()) + ":" + two(d.getMinutes()), cx - ring / 2, cy - 8, ring, 16, "center", 13, true);

  const x = ring + 14;
  g.color(text, 0.95);
  g.text("Écoute : " + dur(listened), x, h / 2 - 26, w - x, 16, "left", 12, true);
  g.color(dim, 1);
  g.text(tracks + (tracks > 1 ? " morceaux" : " morceau") + " cette session", x, h / 2 - 8, w - x, 16, "left", 11, false);
  g.color(accent, 1);
  g.text(p.format || "—", x, h / 2 + 10, w - x, 16, "left", 11, true);
}
