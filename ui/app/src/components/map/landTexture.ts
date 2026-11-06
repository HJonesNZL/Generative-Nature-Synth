/**
 * Paints the globe's equirectangular texture from bundled Natural Earth rings:
 * cream ocean, forest-green hatched land ("woodgrain" from the reference),
 * coast strokes and a 15° graticule. Generated once per globe mount.
 */
import { LAND_110M, NZ_50M, type Ring } from "./geo";

export function paintLandTexture(width: number): HTMLCanvasElement {
  const height = width / 2;
  const canvas = document.createElement("canvas");
  canvas.width = width;
  canvas.height = height;
  const ctx = canvas.getContext("2d");
  if (!ctx) throw new Error("2D canvas unavailable for globe texture");
  const s = width / 4096; // stroke scale relative to the 4096 reference

  ctx.fillStyle = "#eae5d7";
  ctx.fillRect(0, 0, width, height);

  ctx.strokeStyle = "rgba(31, 90, 50, 0.18)";
  ctx.lineWidth = 2 * s;
  for (let lon = -180; lon <= 180; lon += 15) {
    const x = ((lon + 180) / 360) * width;
    ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, height); ctx.stroke();
  }
  for (let lat = -75; lat <= 75; lat += 15) {
    const y = ((90 - lat) / 180) * height;
    ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(width, y); ctx.stroke();
  }

  const hatch = document.createElement("canvas");
  hatch.width = 4;
  hatch.height = Math.max(4, Math.round(9 * s));
  const hctx = hatch.getContext("2d")!;
  hctx.fillStyle = "#e3decd";
  hctx.fillRect(0, 0, hatch.width, hatch.height);
  hctx.fillStyle = "rgba(31, 90, 50, 0.55)";
  hctx.fillRect(0, 0, hatch.width, Math.max(1, Math.round(3 * s)));
  const pattern = ctx.createPattern(hatch, "repeat");

  // Rings are longitude-unwrapped (may exceed ±180°); draw each copy that overlaps the texture.
  const offsetsFor = (ring: Ring) => {
    let min = Infinity;
    let max = -Infinity;
    for (const [lon] of ring) { if (lon < min) min = lon; if (lon > max) max = lon; }
    return [-360, 0, 360].filter((o) => max + o > -180 && min + o < 180);
  };
  const trace = (ring: Ring, offset: number) => {
    ctx.beginPath();
    ring.forEach(([lon, lat], i) => {
      const x = ((lon + offset + 180) / 360) * width;
      const y = ((90 - lat) / 180) * height;
      if (i) ctx.lineTo(x, y); else ctx.moveTo(x, y);
    });
    ctx.closePath();
  };
  const detailedNz = (ring: Ring) => ring.some(([lon, lat]) => lon > 165 && lon < 180 && lat < -33 && lat > -48);
  const rings = [...LAND_110M.filter((r) => !detailedNz(r)), ...NZ_50M];
  ctx.fillStyle = pattern ?? "#c9d6c0";
  for (const ring of rings) {
    for (const offset of offsetsFor(ring)) { trace(ring, offset); ctx.fill(); }
  }
  ctx.strokeStyle = "#1f5a32";
  ctx.lineWidth = 2.4 * s;
  ctx.lineJoin = "round";
  for (const ring of rings) {
    for (const offset of offsetsFor(ring)) {
      // Skip the artificial closing edges along the poles.
      ctx.beginPath();
      let pen = false;
      for (let i = 0; i < ring.length; i += 1) {
        const [lon, lat] = ring[i]!;
        if (Math.abs(lat) >= 89.9) { pen = false; continue; }
        const x = ((lon + offset + 180) / 360) * width;
        const y = ((90 - lat) / 180) * height;
        if (pen) ctx.lineTo(x, y); else { ctx.moveTo(x, y); pen = true; }
      }
      ctx.stroke();
    }
  }
  return canvas;
}
