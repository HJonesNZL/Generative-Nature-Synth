/**
 * Bundled Natural Earth geometry (public domain) and shared map types.
 * Coordinates are WGS84 [longitude, latitude]. Never replace these with
 * generated imagery: they are the cartographic source for both renderers.
 */
import land110 from "../../assets/geo/land-110m.json";
import nz50 from "../../assets/geo/nz-50m.json";

export type Ring = Array<[number, number]>;
export const LAND_110M: Ring[] = (land110 as unknown as { rings: Ring[] }).rings;
export const NZ_50M: Ring[] = (nz50 as unknown as { rings: Ring[] }).rings;

export type MarkerKind = "station" | "fix";
export type MarkerEmphasis = "highlight" | "normal" | "dim";

/** One map symbol. Stations aggregate many detections; fixes are single measured positions. */
export interface MapMarker {
  key: string;              // "station:<id>" or "fix:<eventId>"
  kind: MarkerKind;
  latitude: number;
  longitude: number;
  emphasis: MarkerEmphasis;
  label: string;            // accessible name, e.g. "Rotorua station, 6 Tūī detections in 10 min"
  uncertaintyMetres: number | null;
}

export interface MapRegion { id: string; name: string; ring: Ring; selected: boolean }

export interface Bounds { west: number; east: number; south: number; north: number }
export const BOUNDS_WORLD: Bounds = { west: -180, east: 180, south: -90, north: 90 };
export const BOUNDS_NZ: Bounds = { west: 165.6, east: 179.2, south: -47.6, north: -34.0 };
export const BOUNDS_SW_PACIFIC: Bounds = { west: 140, east: 200, south: -52, north: -8 };

/** Shift negative longitudes east of the antimeridian when the view crosses 180°. */
export function unwrapLongitude(longitude: number, bounds: Bounds): number {
  return bounds.east > 180 && longitude < 0 ? longitude + 360 : longitude;
}
