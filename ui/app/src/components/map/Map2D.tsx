/**
 * SVG equirectangular map. Used for the NZ loupe and as the full 2D fallback
 * when WebGL is unavailable, lost, or the user chooses the 2D view. It needs no
 * GPU and keeps every station selectable by keyboard.
 */
import { memo, useMemo, type KeyboardEvent } from "react";
import { BOUNDS_NZ, LAND_110M, NZ_50M, unwrapLongitude, type Bounds, type MapMarker, type MapRegion, type Ring } from "./geo";

interface Props {
  bounds: Bounds;
  markers: MapMarker[];
  regions: MapRegion[];
  selectedKey: string | null;
  onPick: (key: string) => void;
  title: string;
  showLabels?: boolean;
  interactiveMarkers?: boolean;
}

const WIDTH = 1000;

function useProjection(bounds: Bounds) {
  return useMemo(() => {
    const midLat = ((bounds.north + bounds.south) / 2) * (Math.PI / 180);
    const k = Math.cos(midLat);
    const sx = WIDTH / ((bounds.east - bounds.west) * k);
    const height = (bounds.north - bounds.south) * sx;
    const project = (lon: number, lat: number): [number, number] => [(lon - bounds.west) * k * sx, (bounds.north - lat) * sx];
    const projectPoint = (lon: number, lat: number): [number, number] => project(unwrapLongitude(lon, bounds), lat);
    const metresToUnits = (m: number) => (m / 111_320) * sx;
    return { project, projectPoint, height, metresToUnits };
  }, [bounds]);
}

/** Rings may be longitude-unwrapped; emit every ±360° copy that overlaps the view. */
function ringPath(ring: Ring, project: (lon: number, lat: number) => [number, number], bounds: Bounds): string {
  let minLon = Infinity, maxLon = -Infinity, minLat = Infinity, maxLat = -Infinity;
  for (const [lon, lat] of ring) {
    if (lon < minLon) minLon = lon; if (lon > maxLon) maxLon = lon;
    if (lat < minLat) minLat = lat; if (lat > maxLat) maxLat = lat;
  }
  if (maxLat < bounds.south - 5 || minLat > bounds.north + 5) return "";
  let d = "";
  for (const offset of [-360, 0, 360]) {
    if (maxLon + offset < bounds.west - 5 || minLon + offset > bounds.east + 5) continue;
    ring.forEach(([lon, lat], i) => {
      const [x, y] = project(lon + offset, lat);
      d += `${i ? "L" : "M"}${x.toFixed(1)} ${y.toFixed(1)}`;
    });
    d += "Z";
  }
  return d;
}

const Land = memo(function Land({ bounds }: { bounds: Bounds }) {
  const { project } = useProjection(bounds);
  const detailed = bounds.west >= 160 && bounds.east <= 185 && bounds.south >= -50;
  const d = useMemo(() => {
    const coarse = LAND_110M.filter((ring) => !detailed || !ring.some(([lon, lat]) => lon > 165 && lon < 180 && lat < -33 && lat > -48));
    return [...coarse, ...NZ_50M].map((r) => ringPath(r, project, bounds)).join("");
  }, [bounds, detailed, project]);
  return <path d={d} className="map2d__land" />;
});

export function Map2D({ bounds, markers, regions, selectedKey, onPick, title, showLabels = false, interactiveMarkers = true }: Props) {
  const { project, projectPoint, height, metresToUnits } = useProjection(bounds);
  const scale = bounds === BOUNDS_NZ ? 1 : 0.7;
  const graticule = useMemo(() => {
    const step = bounds.east - bounds.west > 30 ? 10 : 2;
    const lines: string[] = [];
    for (let lon = Math.ceil(bounds.west / step) * step; lon <= bounds.east; lon += step) {
      const [x] = project(lon, 0);
      lines.push(`M${x.toFixed(1)} 0V${height.toFixed(1)}`);
    }
    for (let lat = Math.ceil(bounds.south / step) * step; lat <= bounds.north; lat += step) {
      const [, y] = project(bounds.west, lat);
      lines.push(`M0 ${y.toFixed(1)}H${WIDTH}`);
    }
    return lines.join("");
  }, [bounds, height, project]);

  const onKey = (e: KeyboardEvent, key: string) => {
    if (e.key === "Enter" || e.key === " ") { e.preventDefault(); onPick(key); }
  };

  return (
    <svg className="map2d" viewBox={`0 0 ${WIDTH} ${height.toFixed(1)}`} role="group" aria-label={title} preserveAspectRatio="xMidYMid meet">
      <path d={graticule} className="map2d__graticule" />
      <Land bounds={bounds} />
      {regions.map((r) => (
        <path key={r.id} d={ringPath(r.ring, project, bounds)} className={`map2d__region${r.selected ? " is-selected" : ""}`} aria-hidden="true" />
      ))}
      {markers.map((m) => {
        const [x, y] = projectPoint(m.longitude, m.latitude);
        const selected = m.key === selectedKey;
        const r = (m.emphasis === "highlight" ? 13 : 10) * scale;
        const symbol = m.kind === "fix"
          ? <path d={`M${x} ${y - r}L${x + r} ${y}L${x} ${y + r}L${x - r} ${y}Z`} className={`map2d__fix is-${m.emphasis}`} />
          : <circle cx={x} cy={y} r={r} className={`map2d__station is-${m.emphasis}`} />;
        return (
          <g
            key={m.key}
            className={`map2d__marker${selected ? " is-selected" : ""}`}
            role={interactiveMarkers ? "button" : undefined}
            tabIndex={interactiveMarkers ? 0 : undefined}
            aria-label={interactiveMarkers ? m.label : undefined}
            aria-pressed={interactiveMarkers ? selected : undefined}
            onClick={interactiveMarkers ? () => onPick(m.key) : undefined}
            onKeyDown={interactiveMarkers ? (e) => onKey(e, m.key) : undefined}
          >
            {m.uncertaintyMetres ? <circle cx={x} cy={y} r={Math.max(r * 1.6, metresToUnits(m.uncertaintyMetres))} className="map2d__uncertainty" /> : null}
            {symbol}
            {selected ? <circle cx={x} cy={y} r={r * 2.4} className="map2d__selection" /> : null}
            {showLabels && (selected || m.emphasis === "highlight") ? <text x={x + r * 1.6} y={y + 4} className="map2d__label">{m.label.split(",")[0]}</text> : null}
          </g>
        );
      })}
    </svg>
  );
}
