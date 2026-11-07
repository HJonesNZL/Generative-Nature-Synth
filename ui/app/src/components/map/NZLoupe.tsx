import { useEffect, useRef, useState, type KeyboardEvent, type PointerEvent } from "react";
import { Icon } from "../Icon";
import { Map2D } from "./Map2D";
import { BOUNDS_NZ, unwrapLongitude, type MapLocateRequest, type MapMarker, type MapRegion } from "./geo";
import { clampLoupePan, dragLoupePan, loupeViewport, mapProjection, LOUPE_DRAG_THRESHOLD_PX, type LoupePan } from "./loupeGeometry";

interface Props {
  latestMarkerKey?: string | null;
  recentMarkerKeys?: ReadonlySet<string>;
  locateRequest?: MapLocateRequest | null;
  markers: MapMarker[];
  regions: MapRegion[];
  selectedKey: string | null;
  onPick: (key: string) => void;
  pan: LoupePan;
  onPanChange: (pan: LoupePan) => void;
}

interface Drag {
  pointerId: number;
  target: Element;
  startX: number;
  startY: number;
  widthPx: number;
  startPan: LoupePan;
  moved: boolean;
}

export function NZLoupe({ markers, regions, selectedKey, latestMarkerKey, recentMarkerKeys, locateRequest, onPick, pan, onPanChange }: Props) {
  const drag = useRef<Drag | null>(null);
  const locatedSequence = useRef<number | null>(null);
  const suppressClick = useRef(false);
  const [dragging, setDragging] = useState(false);
  const viewport = loupeViewport(BOUNDS_NZ, pan);
  const projection = mapProjection(BOUNDS_NZ);
  const visibleMarkers = markers.filter((marker) => {
    const [x, y] = projection.project(unwrapLongitude(marker.longitude, BOUNDS_NZ), marker.latitude);
    return Math.hypot(x - viewport.x - viewport.width / 2, y - viewport.y - viewport.height / 2) <= viewport.width / 2 - 20;
  });
  useEffect(() => {
    if (!locateRequest || locatedSequence.current === locateRequest.seq) return;
    locatedSequence.current = locateRequest.seq;
    const { latitude, longitude } = locateRequest;
    const lon = unwrapLongitude(longitude, BOUNDS_NZ);
    if (!Number.isFinite(latitude) || !Number.isFinite(lon) || latitude < BOUNDS_NZ.south || latitude > BOUNDS_NZ.north || lon < BOUNDS_NZ.west || lon > BOUNDS_NZ.east) return;
    const projection = mapProjection(BOUNDS_NZ);
    const [x, y] = projection.project(lon, latitude);
    onPanChange(clampLoupePan({ x: 500 - x, y: projection.height * 0.34 - y }));
  }, [locateRequest, onPanChange]);
  const reset = () => onPanChange({ x: 0, y: 0 });

  const start = (event: PointerEvent<HTMLDivElement>) => {
    event.stopPropagation();
    if (!event.isPrimary || event.button !== 0 || drag.current) return;
    suppressClick.current = false;
    const target = event.target as Element;
    // Capture the original hit target so a stationary marker press remains a native click.
    target.setPointerCapture(event.pointerId);
    drag.current = { pointerId: event.pointerId, target, startX: event.clientX, startY: event.clientY,
      widthPx: event.currentTarget.clientWidth, startPan: clampLoupePan(pan), moved: false };
  };

  const move = (event: PointerEvent<HTMLDivElement>) => {
    event.stopPropagation();
    const current = drag.current;
    if (!current || current.pointerId !== event.pointerId) return;
    const dx = event.clientX - current.startX;
    const dy = event.clientY - current.startY;
    if (!current.moved && Math.hypot(dx, dy) < LOUPE_DRAG_THRESHOLD_PX) return;
    if (!current.moved) {
      // The marker may leave the viewport during a drag; retain capture on the fixed surface.
      event.currentTarget.setPointerCapture(event.pointerId);
      current.target = event.currentTarget;
    }
    current.moved = true;
    suppressClick.current = true;
    setDragging(true);
    event.preventDefault();
    onPanChange(dragLoupePan(current.startPan, dx, dy, current.widthPx, viewport.width));
  };

  const finish = (event: PointerEvent<HTMLDivElement>) => {
    event.stopPropagation();
    const current = drag.current;
    if (!current || current.pointerId !== event.pointerId) return;
    if (event.type === "lostpointercapture" && current.target.hasPointerCapture(event.pointerId)) return;
    if (event.type === "pointerup") move(event);
    drag.current = null;
    setDragging(false);
    if (current.target.hasPointerCapture(event.pointerId)) current.target.releasePointerCapture(event.pointerId);
  };

  const onKey = (event: KeyboardEvent<HTMLDivElement>) => {
    if (event.altKey || event.ctrlKey || event.metaKey) return;
    const step = event.shiftKey ? 120 : 60;
    const delta = { ArrowLeft: [-step, 0], ArrowRight: [step, 0], ArrowUp: [0, -step], ArrowDown: [0, step] }[event.key];
    if (event.key !== "Home" && !delta) return;
    event.preventDefault();
    event.stopPropagation();
    if (event.key === "Home") reset();
    else if (delta) onPanChange(clampLoupePan({ x: pan.x + delta[0]!, y: pan.y + delta[1]! }));
  };

  return (
    <div className="loupe" aria-label="New Zealand detail">
      <div className={`loupe__pan${dragging ? " is-dragging" : ""}`} role="group" tabIndex={0}
        aria-label="Pan New Zealand detail map" aria-description="Drag to move the map. Arrow keys pan; Home resets the view."
        onPointerDown={start} onPointerMove={move} onPointerUp={finish} onPointerCancel={finish} onLostPointerCapture={finish}
        onKeyDown={onKey} onClickCapture={(event) => {
          if (suppressClick.current && event.detail !== 0) { event.preventDefault(); event.stopPropagation(); }
        }}>
        <Map2D bounds={BOUNDS_NZ} viewport={viewport} markers={visibleMarkers} regions={regions} selectedKey={selectedKey} latestMarkerKey={latestMarkerKey} recentMarkerKeys={recentMarkerKeys}
          onPick={onPick} title="New Zealand detail map. Locations are buttons." />
      </div>
      <button type="button" className="loupe__reset pill pill--quiet" onClick={reset} aria-label="Reset New Zealand view">
        <Icon name="recenter" size={11} />Reset view
      </button>
    </div>
  );
}
