// NX Player — plug-in d'interface « VU-mètres analogiques » (MIT).
// Aiguilles avec inertie mécanique simulée (ressort amorti), 0 VU calé sur −18 dBFS.

const needles = [{ pos: 0, vel: 0 }, { pos: 0, vel: 0 }];
let last = 0;

function vuFromRms(rms) {
  const dbfs = 20 * Math.log10(Math.max(rms, 1e-6));
  const vu = dbfs + 18;                  // 0 VU = -18 dBFS
  return Math.min(1, Math.max(0, (vu + 20) / 23)); // -20..+3 VU -> 0..1
}

function drawMeter(g, x, y, w, h, value, peak, label, nx) {
  const text = nx.theme ? nx.theme.text : "#FFFFFF";
  const accent = nx.theme ? nx.theme.accent : "#00F5A0";
  g.color("#F2E8D0", 0.92);
  g.roundRect(x, y, w, h, 10);
  g.color("#000000", 0.25);
  g.strokeRect(x, y, w, h, 1);

  const cx = x + w / 2, cy = y + h * 0.95, r = h * 0.78;
  const a0 = -0.85, a1 = 0.85;          // radians, 0 = vertical
  // scale
  g.color("#3A3326", 0.9);
  g.arc(cx, cy, r, a0, a1 * 0.62, 2);
  g.color("#C0392B", 0.95);
  g.arc(cx, cy, r, a1 * 0.62, a1, 4);
  const marks = [-20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3];
  for (const m of marks) {
    const t = (m + 20) / 23;
    const a = a0 + (a1 - a0) * t;
    const sx = cx + Math.sin(a) * r, sy = cy - Math.cos(a) * r;
    const ex = cx + Math.sin(a) * (r - 8), ey = cy - Math.cos(a) * (r - 8);
    g.color(m > 0 ? "#C0392B" : "#3A3326", 0.9);
    g.line(sx, sy, ex, ey, 1.5);
    if (m === -20 || m === -10 || m === -5 || m === 0 || m === 3) {
      const tx = cx + Math.sin(a) * (r - 22), ty = cy - Math.cos(a) * (r - 22);
      g.text(String(m), tx - 12, ty - 7, 24, 14, "center", 10, false);
    }
  }
  g.color("#3A3326", 0.85);
  g.text("VU", cx - 20, y + h * 0.48, 40, 18, "center", 14, true);
  g.text(label, x + 8, y + h - 20, 30, 14, "left", 10, true);

  // needle
  const a = a0 + (a1 - a0) * value;
  g.color("#111111", 0.95);
  g.line(cx, cy, cx + Math.sin(a) * (r + 4), cy - Math.cos(a) * (r + 4), 1.8);
  g.color("#222222");
  g.ellipse(cx - 5, cy - 5, 10, 10);

  // peak LED
  g.color(peak > 0.89 ? "#FF3B30" : "#5A1A14", 1);
  g.ellipse(x + w - 22, y + 10, 10, 10);
  g.color(accent, 0.0);
}

function render(g, w, h, nx, slot) {
  const now = nx.time || 0;
  const dt = Math.min(0.05, Math.max(0.001, now - last));
  last = now;
  const a = nx.audio || {};
  const targets = [vuFromRms(a.rmsL || 0), vuFromRms(a.rmsR || 0)];
  // damped spring: overshoot ~1.5 %, like a real VU movement
  for (let i = 0; i < 2; i++) {
    const n = needles[i];
    const k = 180, c = 24;
    const acc = k * (targets[i] - n.pos) - c * n.vel;
    n.vel += acc * dt;
    n.pos = Math.min(1.05, Math.max(0, n.pos + n.vel * dt));
  }
  const gap = 10;
  const mw = (w - gap) / 2;
  const mh = Math.min(h, mw * 0.62);
  const y = (h - mh) / 2;
  drawMeter(g, 0, y, mw, mh, needles[0].pos, a.peakL || 0, "L", nx);
  drawMeter(g, mw + gap, y, mw, mh, needles[1].pos, a.peakR || 0, "R", nx);
}
