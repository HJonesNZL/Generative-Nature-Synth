import type { Bounds } from "./geo";

export interface LoupePan { x: number; y: number }
export interface MapViewport { x: number; y: number; width: number; height: number }

export const MAP_WIDTH = 1000;
export const LOUPE_DRAG_THRESHOLD_PX = 5;
export const LOUPE_PAN_LIMITS = { minX: -350, maxX: 350, minY: -850, maxY: 300 };

/** Fixed equirectangular projection: changing the viewport never changes coordinates. */
export function mapProjection(bounds: Bounds) {
  const longitudeScale = Math.cos((bounds.north + bounds.south) / 2 * Math.PI / 180);
  const latitudeScale = MAP_WIDTH / ((bounds.east - bounds.west) * longitudeScale);
  const height = (bounds.north - bounds.south) * latitudeScale;
  return {
    height,
    project: (longitude: number, latitude: number): [number, number] => [
      (longitude - bounds.west) * longitudeScale * latitudeScale,
      (bounds.north - latitude) * latitudeScale,
    ],
    metresToUnits: (metres: number) => metres / 111_320 * latitudeScale,
  };
}

export function clampLoupePan(pan: LoupePan): LoupePan {
  if (!Number.isFinite(pan.x) || !Number.isFinite(pan.y)) return { x: 0, y: 0 };
  return {
    x: Math.max(LOUPE_PAN_LIMITS.minX, Math.min(LOUPE_PAN_LIMITS.maxX, pan.x)),
    y: Math.max(LOUPE_PAN_LIMITS.minY, Math.min(LOUPE_PAN_LIMITS.maxY, pan.y)),
  };
}

export function loupeViewport(bounds: Bounds, pan: LoupePan): MapViewport {
  const height = mapProjection(bounds).height;
  // Keep the useful enlarged detail scale. Pan bounds reach North, South and Stewart islands.
  const side = MAP_WIDTH;
  const bounded = clampLoupePan(pan);
  return { x: -bounded.x, y: height * 0.34 - side / 2 - bounded.y, width: side, height: side };
}

export function dragLoupePan(start: LoupePan, dxPx: number, dyPx: number, widthPx: number, viewWidth: number): LoupePan {
  if (!(widthPx > 0)) return clampLoupePan(start);
  return clampLoupePan({ x: start.x + dxPx * viewWidth / widthPx, y: start.y + dyPx * viewWidth / widthPx });
}
