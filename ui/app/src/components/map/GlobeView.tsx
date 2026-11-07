/**
 * Three.js globe (WebGL). Visual only — it never times or triggers sound.
 *
 * Budgets: one 96×64 sphere, one land texture (≤ 4096 px), MAX_MARKERS
 * instanced station discs + rings, MAX_MARKERS instanced fix diamonds,
 * one bounded playback-origin point layer, region outlines as dashed lines. Frame rate is
 * capped at 30 fps and rendering stops entirely when nothing changes, when
 * the document is hidden, or when the component unmounts (Sound Studio).
 * Pixel ratio is capped at 2. All GPU resources are disposed on teardown.
 * On `webglcontextlost` the parent is told to switch to the SVG map.
 */
import { useEffect, useRef, type KeyboardEvent } from "react";
import * as THREE from "three";
import { OrbitControls } from "three/addons/controls/OrbitControls.js";
import { paintLandTexture } from "./landTexture";
import type { MapLocateRequest, MapMarker, MapRegion } from "./geo";
import { MAX_SOUND_ORIGINS, type SoundingOrigin } from "./soundingOrigins";

const MAX_MARKERS = 400;
const FRAME_MS = 1000 / 30;

const COLORS = {
  accent: new THREE.Color("#1f5a32"),
  normal: new THREE.Color("#eeeadf"),
  dim: new THREE.Color("#a9b8a6"),
  fix: new THREE.Color("#3d5a6c"),
};

export function latLonToVector(lat: number, lon: number, radius = 1): THREE.Vector3 {
  // Matches THREE.SphereGeometry UVs with an equirectangular texture whose u=0 is −180°.
  const phi = ((lon + 180) / 360) * Math.PI * 2;
  const theta = ((90 - lat) / 180) * Math.PI;
  return new THREE.Vector3(-radius * Math.cos(phi) * Math.sin(theta), radius * Math.cos(theta), radius * Math.sin(phi) * Math.sin(theta));
}
function vectorToLatLon(v: THREE.Vector3): { latitude: number; longitude: number } {
  const n = v.clone().normalize();
  const latitude = 90 - (Math.acos(n.y) * 180) / Math.PI;
  let longitude = (Math.atan2(n.z, -n.x) * 180) / Math.PI - 180;
  if (longitude < -180) longitude += 360;
  return { latitude, longitude };
}

export interface GlobeProps {
  latestMarkerKey?: string | null;
  recentMarkerKeys?: ReadonlySet<string>;
  locateRequest?: MapLocateRequest | null;
  markers: MapMarker[];
  regions: MapRegion[];
  selectedKey: string | null;
  orientation: { longitude: number; latitude: number; distance: number };
  reducedMotion: boolean;
  soundingOrigins: readonly SoundingOrigin[];
  sampleSoundingOrigins: () => readonly SoundingOrigin[];
  soundingVersion: string;
  onPick: (key: string) => void;
  onOrientationChange: (o: { longitude: number; latitude: number; distance: number }) => void;
  onContextLost: () => void;
  zoomCommand: { seq: number; action: "in" | "out" | "reset" } | null;
}

interface GlobeRuntime {
  renderer: THREE.WebGLRenderer;
  scene: THREE.Scene;
  camera: THREE.PerspectiveCamera;
  controls: OrbitControls;
  discs: THREE.InstancedMesh;
  rings: THREE.InstancedMesh;
  fixes: THREE.InstancedMesh;
  reported: THREE.InstancedMesh;
  selection: THREE.Mesh;
  latest: THREE.Mesh;
  recent: THREE.InstancedMesh;
  soundPoints: THREE.Points;
  regionGroup: THREE.Group;
  globe: THREE.Mesh;
  markerKeys: { discs: string[]; fixes: string[]; reported: string[] };
  requestRender: () => void;
  dispose: () => void;
}

export function GlobeView(props: GlobeProps) {
  const host = useRef<HTMLDivElement>(null);
  const runtime = useRef<GlobeRuntime | null>(null);
  const locatedSequence = useRef<number | null>(null);
  const latestMarkerLabel = props.markers.find((marker) => marker.key === props.latestMarkerKey)?.label;
  const propsRef = useRef(props);
  propsRef.current = props;

  // Mount: create renderer/scene once.
  useEffect(() => {
    const container = host.current!;
    // Each effect owns a fresh canvas: StrictMode may dispose/recreate without remounting the DOM.
    const canvas = document.createElement("canvas");
    canvas.className = "globe__canvas";
    container.appendChild(canvas);
    let renderer: THREE.WebGLRenderer;
    try {
      renderer = new THREE.WebGLRenderer({ canvas, antialias: true, alpha: true, powerPreference: "low-power" });
    } catch {
      canvas.remove();
      propsRef.current.onContextLost();
      return;
    }
    renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
    renderer.setClearColor(0x000000, 0); // transparent: the cream ground continues behind the globe
    renderer.outputColorSpace = THREE.SRGBColorSpace;

    const scene = new THREE.Scene();
    const camera = new THREE.PerspectiveCamera(30, 1, 0.1, 50);
    const { orientation } = propsRef.current;
    camera.position.copy(latLonToVector(orientation.latitude, orientation.longitude, orientation.distance));
    camera.lookAt(0, 0, 0);

    const textureSize = Math.min(4096, renderer.capabilities.maxTextureSize);
    const texture = new THREE.CanvasTexture(paintLandTexture(textureSize));
    texture.colorSpace = THREE.SRGBColorSpace;
    texture.anisotropy = Math.min(4, renderer.capabilities.getMaxAnisotropy());
    const globeGeometry = new THREE.SphereGeometry(1, 96, 64);
    const globeMaterial = new THREE.MeshBasicMaterial({ map: texture });
    const globe = new THREE.Mesh(globeGeometry, globeMaterial);
    scene.add(globe);

    const rimGeometry = new THREE.SphereGeometry(1.012, 64, 48);
    const rimMaterial = new THREE.MeshBasicMaterial({ color: COLORS.accent, side: THREE.BackSide });
    scene.add(new THREE.Mesh(rimGeometry, rimMaterial));

    const discGeometry = new THREE.CircleGeometry(1, 24);
    const ringGeometry = new THREE.RingGeometry(0.72, 1, 24);
    const fixGeometry = new THREE.CircleGeometry(1, 4);
    const reportedGeometry = new THREE.CircleGeometry(1, 3);
    const markerMaterial = new THREE.MeshBasicMaterial({ color: 0xffffff, side: THREE.DoubleSide });
    const ringMaterial = new THREE.MeshBasicMaterial({ color: COLORS.accent, side: THREE.DoubleSide });
    const discs = new THREE.InstancedMesh(discGeometry, markerMaterial, MAX_MARKERS);
    const rings = new THREE.InstancedMesh(ringGeometry, ringMaterial, MAX_MARKERS);
    const fixes = new THREE.InstancedMesh(fixGeometry, markerMaterial, MAX_MARKERS);
    const reported = new THREE.InstancedMesh(reportedGeometry, markerMaterial, MAX_MARKERS);
    for (const m of [discs, rings, fixes, reported]) { m.count = 0; m.frustumCulled = false; scene.add(m); }

    const selection = new THREE.Mesh(new THREE.RingGeometry(0.8, 1, 40), new THREE.MeshBasicMaterial({ color: COLORS.accent, side: THREE.DoubleSide }));
    selection.visible = false;
    scene.add(selection);

    const latest = new THREE.Mesh(new THREE.RingGeometry(0.86, 1, 48), new THREE.MeshBasicMaterial({ color: "#bd6538", side: THREE.DoubleSide }));
    latest.visible = false;
    scene.add(latest);

    const recent = new THREE.InstancedMesh(new THREE.RingGeometry(0.9, 1, 32), new THREE.MeshBasicMaterial({ color: "#bd6538", side: THREE.DoubleSide }), 8);
    recent.count = 0;
    recent.frustumCulled = false;
    scene.add(recent);

    const soundGeometry = new THREE.BufferGeometry();
    const soundPositions = new THREE.BufferAttribute(new Float32Array(MAX_SOUND_ORIGINS * 3), 3);
    const soundOpacity = new THREE.BufferAttribute(new Float32Array(MAX_SOUND_ORIGINS), 1);
    soundPositions.setUsage(THREE.DynamicDrawUsage);
    soundOpacity.setUsage(THREE.DynamicDrawUsage);
    soundGeometry.setAttribute("position",soundPositions);
    soundGeometry.setAttribute("soundOpacity",soundOpacity);
    soundGeometry.setDrawRange(0,0);
    const soundMaterial = new THREE.ShaderMaterial({
      uniforms: {pointSize: {value: 36 * renderer.getPixelRatio()}},
      transparent: true, depthWrite: false,
      vertexShader: `attribute float soundOpacity; varying float opacity; uniform float pointSize;
        void main(){opacity=soundOpacity;gl_Position=projectionMatrix*modelViewMatrix*vec4(position,1.0);gl_PointSize=pointSize;}`,
      fragmentShader: `varying float opacity;
        void main(){float r=distance(gl_PointCoord,vec2(0.5));if(r>0.5)discard;
          float halo=(1.0-smoothstep(0.26,0.5,r))*0.25;
          float core=(1.0-smoothstep(0.12,0.17,r))*0.65;
          float ring=smoothstep(0.30,0.33,r)*(1.0-smoothstep(0.37,0.40,r))*0.55;
          gl_FragColor=vec4(0.95,0.74,0.29,(halo+core+ring)*opacity);}`,
    });
    const soundPoints = new THREE.Points(soundGeometry,soundMaterial);
    soundPoints.frustumCulled=false;
    scene.add(soundPoints);
    const regionGroup = new THREE.Group();
    scene.add(regionGroup);

    const controls = new OrbitControls(camera, canvas);
    controls.enablePan = false;
    controls.minDistance = 1.5;
    controls.maxDistance = 6;
    controls.rotateSpeed = 0.45;
    controls.zoomSpeed = 0.6;
    controls.enableDamping = !propsRef.current.reducedMotion;
    controls.dampingFactor = 0.12;

    let frame = 0;
    let lastFrame = 0;
    let animating = false;
    const tick = (time: number) => {
      frame = 0;
      if (document.hidden) { animating = false; return; }
      if (time - lastFrame < FRAME_MS) { frame = requestAnimationFrame(tick); return; }
      lastFrame = time;
      const moving = controls.update();
      const fading = updateSounding();
      updateLabelPosition();
      renderer.render(scene, camera);
      animating = moving || fading;
      if (animating) frame = requestAnimationFrame(tick);
    };
    const requestRender = () => { if (!frame) frame = requestAnimationFrame(tick); };

    const updateSounding = (): boolean => {
      const origins=propsRef.current.sampleSoundingOrigins();
      const count=Math.min(origins.length,MAX_SOUND_ORIGINS);
      let fading=false;
      for(let i=0;i<count;++i){
        const origin=origins[i]!;
        const point=latLonToVector(origin.latitude,origin.longitude,1.012);
        soundPositions.setXYZ(i,point.x,point.y,point.z);
        soundOpacity.setX(i,origin.level);
        fading ||= origin.level<1;
      }
      soundGeometry.setDrawRange(0,count);
      soundPositions.needsUpdate=true;
      soundOpacity.needsUpdate=true;
      return fading && !propsRef.current.reducedMotion;
    };

    const label = document.createElement("span");
    label.className = "globe__label";
    label.setAttribute("aria-hidden", "true");
    container.appendChild(label);
    const updateLabelPosition = () => {
      if (!selection.visible) { label.style.display = "none"; return; }
      const world = selection.position.clone();
      const facing = world.clone().normalize().dot(camera.position.clone().normalize()) > 0.15;
      const projected = world.project(camera);
      if (!facing) { label.style.display = "none"; return; }
      label.style.display = "block";
      label.style.transform = `translate(${((projected.x + 1) / 2) * container.clientWidth + 14}px, ${((1 - projected.y) / 2) * container.clientHeight - 8}px)`;
    };

    const resize = () => {
      const w = container.clientWidth;
      const h = container.clientHeight;
      if (!w || !h) return;
      renderer.setSize(w, h, false);
      camera.aspect = w / h;
      // Fit the sphere to the shorter canvas edge without changing the user's zoom.
      // A tall native window must not crop the sides of the globe.
      camera.fov = (2 * Math.atan(Math.tan(THREE.MathUtils.degToRad(15)) / Math.min(1, camera.aspect)) * 180) / Math.PI;
      camera.updateProjectionMatrix();
      requestRender();
    };
    const observer = new ResizeObserver(resize);
    observer.observe(container);
    resize();

    const onChange = () => requestRender();
    const onEnd = () => {
      const { latitude, longitude } = vectorToLatLon(camera.position);
      propsRef.current.onOrientationChange({ latitude, longitude, distance: camera.position.length() });
    };
    controls.addEventListener("change", onChange);
    controls.addEventListener("end", onEnd);

    // Picking: a click that did not drag selects the nearest marker under the pointer.
    const raycaster = new THREE.Raycaster();
    let downAt: { x: number; y: number } | null = null;
    const onPointerDown = (e: PointerEvent) => { downAt = { x: e.clientX, y: e.clientY }; };
    const onPointerUp = (e: PointerEvent) => {
      if (!downAt || Math.hypot(e.clientX - downAt.x, e.clientY - downAt.y) > 4) { downAt = null; return; }
      downAt = null;
      const rect = canvas.getBoundingClientRect();
      const pointer = new THREE.Vector2(((e.clientX - rect.left) / rect.width) * 2 - 1, -((e.clientY - rect.top) / rect.height) * 2 + 1);
      raycaster.setFromCamera(pointer, camera);
      const hits = raycaster.intersectObjects([discs, fixes, reported, globe], false);
      const hit = hits[0];
      if (!hit || hit.object === globe || hit.instanceId === undefined) return;
      const keys = hit.object === discs ? runtime.current!.markerKeys.discs : hit.object === reported ? runtime.current!.markerKeys.reported : runtime.current!.markerKeys.fixes;
      const key = keys[hit.instanceId];
      if (key) propsRef.current.onPick(key);
    };
    canvas.addEventListener("pointerdown", onPointerDown);
    canvas.addEventListener("pointerup", onPointerUp);

    const onLost = (e: Event) => { e.preventDefault(); cancelAnimationFrame(frame); frame = 0; propsRef.current.onContextLost(); };
    canvas.addEventListener("webglcontextlost", onLost);
    const onVisibility = () => { if (!document.hidden) requestRender(); };
    document.addEventListener("visibilitychange", onVisibility);

    const disposables: Array<{ dispose: () => void }> = [texture, globeGeometry, globeMaterial, rimGeometry, rimMaterial, discGeometry, ringGeometry, fixGeometry, reportedGeometry, markerMaterial, ringMaterial, soundGeometry, soundMaterial, selection.geometry, selection.material as THREE.Material, latest.geometry, latest.material as THREE.Material, recent.geometry, recent.material as THREE.Material, controls];
    runtime.current = {
      renderer, scene, camera, controls, discs, rings, fixes, reported, selection, latest, recent, soundPoints, regionGroup, globe,
      markerKeys: { discs: [], fixes: [], reported: [] },
      requestRender,
      dispose: () => {
        cancelAnimationFrame(frame);
        observer.disconnect();
        controls.removeEventListener("change", onChange);
        controls.removeEventListener("end", onEnd);
        canvas.removeEventListener("pointerdown", onPointerDown);
        canvas.removeEventListener("pointerup", onPointerUp);
        canvas.removeEventListener("webglcontextlost", onLost);
        document.removeEventListener("visibilitychange", onVisibility);
        for (const child of regionGroup.children) { const line = child as THREE.Line; line.geometry.dispose(); (line.material as THREE.Material).dispose(); }
        for (const d of disposables) d.dispose();
        discs.dispose(); rings.dispose(); fixes.dispose(); reported.dispose(); recent.dispose();
        label.remove();
        renderer.dispose();
        canvas.remove();
        renderer.forceContextLoss(); // release the GL context now rather than waiting for GC (webviews cap live contexts)
      },
    };
    requestRender();
    return () => { runtime.current?.dispose(); runtime.current = null; };
  }, []);

  // Markers and selection (bounded; only rebuilt when the aggregated marker list changes).
  useEffect(() => {
    const rt = runtime.current;
    if (!rt) return;
    const matrix = new THREE.Matrix4();
    const quaternion = new THREE.Quaternion();
    const scale = new THREE.Vector3();
    const up = new THREE.Vector3(0, 0, 1);
    let d = 0;
    let f = 0;
    let o = 0;
    rt.markerKeys = { discs: [], fixes: [], reported: [] };
    for (const m of props.markers.slice(0, MAX_MARKERS)) {
      const pos = latLonToVector(m.latitude, m.longitude, 1.004);
      quaternion.setFromUnitVectors(up, pos.clone().normalize());
      const size = m.emphasis === "highlight" ? 0.016 : 0.011;
      scale.setScalar(size);
      matrix.compose(pos, quaternion, scale);
      const color = m.emphasis === "highlight" ? COLORS.accent : m.emphasis === "dim" ? COLORS.dim : COLORS.normal;
      if (m.kind === "reported") {
        rt.reported.setMatrixAt(o, matrix);
        rt.reported.setColorAt(o, color);
        rt.markerKeys.reported.push(m.key);
        o += 1;
      } else if (m.kind === "fix") {
        rt.fixes.setMatrixAt(f, matrix);
        rt.fixes.setColorAt(f, m.emphasis === "dim" ? COLORS.dim : COLORS.fix);
        rt.markerKeys.fixes.push(m.key);
        f += 1;
      } else {
        rt.discs.setMatrixAt(d, matrix);
        rt.discs.setColorAt(d, color);
        rt.rings.setMatrixAt(d, matrix);
        rt.markerKeys.discs.push(m.key);
        d += 1;
      }
      if (m.key === props.selectedKey) {
        rt.selection.position.copy(latLonToVector(m.latitude, m.longitude, 1.005));
        rt.selection.quaternion.copy(quaternion);
        rt.selection.scale.setScalar(size * 2.2);
        const label = host.current?.querySelector(".globe__label");
        if (label) label.textContent = m.label.split(",")[0] ?? "";
      }
    }
    const latestMarker = props.markers.find((m) => m.key === props.latestMarkerKey);
    rt.latest.visible = Boolean(latestMarker);
    if (latestMarker) {
      const position = latLonToVector(latestMarker.latitude, latestMarker.longitude, 1.008);
      rt.latest.position.copy(position);
      rt.latest.quaternion.setFromUnitVectors(up, position.clone().normalize());
      rt.latest.scale.setScalar(0.045);
    }
    let recentCount = 0;
    for (const marker of props.markers) {
      if (recentCount === 8) break;
      if (marker.key === props.latestMarkerKey || !props.recentMarkerKeys?.has(marker.key)) continue;
      const position = latLonToVector(marker.latitude, marker.longitude, 1.007);
      quaternion.setFromUnitVectors(up, position.clone().normalize());
      scale.setScalar((marker.emphasis === "highlight" ? 0.016 : 0.011) * 1.6);
      matrix.compose(position, quaternion, scale);
      rt.recent.setMatrixAt(recentCount++, matrix);
    }
    rt.recent.count = recentCount;
    rt.recent.instanceMatrix.needsUpdate = true;
    rt.discs.count = d;
    rt.rings.count = d;
    rt.fixes.count = f;
    rt.reported.count = o;
    rt.selection.visible = props.markers.some((m) => m.key === props.selectedKey);
    for (const mesh of [rt.discs, rt.rings, rt.fixes, rt.reported]) {
      mesh.instanceMatrix.needsUpdate = true;
      if (mesh.instanceColor) mesh.instanceColor.needsUpdate = true;
    }
    rt.requestRender();
  }, [props.markers, props.selectedKey, props.latestMarkerKey, props.recentMarkerKeys]);

  // Region outlines.
  useEffect(() => {
    const rt = runtime.current;
    if (!rt) return;
    for (const child of [...rt.regionGroup.children]) {
      const line = child as THREE.Line;
      line.geometry.dispose();
      (line.material as THREE.Material).dispose();
      rt.regionGroup.remove(line);
    }
    for (const region of props.regions) {
      const points: THREE.Vector3[] = [];
      for (let i = 0; i < region.ring.length - 1; i += 1) {
        const [lon0, lat0] = region.ring[i]!;
        const [lon1, lat1] = region.ring[i + 1]!;
        for (let s = 0; s < 8; s += 1) points.push(latLonToVector(lat0 + ((lat1 - lat0) * s) / 8, lon0 + ((lon1 - lon0) * s) / 8, 1.003));
      }
      points.push(points[0]!.clone());
      const geometry = new THREE.BufferGeometry().setFromPoints(points);
      const material = new THREE.LineDashedMaterial({ color: COLORS.accent, dashSize: 0.012, gapSize: 0.008, transparent: true, opacity: region.selected ? 1 : 0.55 });
      const line = new THREE.Line(geometry, material);
      line.computeLineDistances();
      rt.regionGroup.add(line);
    }
    rt.requestRender();
  }, [props.regions]);

  useEffect(() => { runtime.current?.requestRender(); }, [props.soundingVersion]);
  useEffect(() => { if (runtime.current) runtime.current.controls.enableDamping = !props.reducedMotion; }, [props.reducedMotion]);

  useEffect(() => {
    const rt = runtime.current;
    const cmd = props.zoomCommand;
    if (!rt || !cmd) return;
    if (cmd.action === "reset") rt.camera.position.copy(latLonToVector(-34, 172, 4.1));
    else rt.camera.position.multiplyScalar(cmd.action === "in" ? 0.85 : 1.18);
    rt.camera.position.setLength(Math.min(6, Math.max(1.5, rt.camera.position.length())));
    rt.camera.lookAt(0, 0, 0);
    rt.controls.update();
    rt.requestRender();
    const { latitude, longitude } = vectorToLatLon(rt.camera.position);
    propsRef.current.onOrientationChange({ latitude, longitude, distance: rt.camera.position.length() });
  }, [props.zoomCommand]);

  useEffect(() => {
    const rt = runtime.current;
    const request = props.locateRequest;
    if (!rt || !request || locatedSequence.current === request.seq || !Number.isFinite(request.latitude) || !Number.isFinite(request.longitude)) return;
    locatedSequence.current = request.seq;
    const distance = rt.camera.position.length();
    rt.camera.position.copy(latLonToVector(request.latitude, request.longitude, distance));
    rt.camera.lookAt(0, 0, 0);
    rt.controls.update();
    rt.requestRender();
    propsRef.current.onOrientationChange({ latitude: request.latitude, longitude: request.longitude, distance });
  }, [props.locateRequest]);

  // Keyboard rotation on the focused globe (no global key handlers).
  const onKeyDown = (e: KeyboardEvent) => {
    const rt = runtime.current;
    if (!rt) return;
    const step = e.shiftKey ? 1 : 5;
    const { latitude, longitude } = vectorToLatLon(rt.camera.position);
    let lat = latitude;
    let lon = longitude;
    if (e.key === "ArrowLeft") lon -= step;
    else if (e.key === "ArrowRight") lon += step;
    else if (e.key === "ArrowUp") lat = Math.min(80, lat + step);
    else if (e.key === "ArrowDown") lat = Math.max(-80, lat - step);
    else return;
    e.preventDefault();
    rt.camera.position.copy(latLonToVector(lat, lon, rt.camera.position.length()));
    rt.camera.lookAt(0, 0, 0);
    rt.controls.update();
    rt.requestRender();
    propsRef.current.onOrientationChange({ latitude: lat, longitude: lon, distance: rt.camera.position.length() });
  };

  return (
    <div ref={host} className="globe" data-sounding-origins={props.soundingOrigins.length} data-sounding-tokens={props.soundingOrigins.flatMap(origin => origin.tokens).join(" ")} tabIndex={0} role="application" aria-roledescription="globe" aria-label={`Globe. Drag or use arrow keys to rotate; use the station list or 2D map to select by keyboard.${latestMarkerLabel ? ` Latest detection: ${latestMarkerLabel}.` : ""}`} onKeyDown={onKeyDown}>
      <span className="sr-only">{props.soundingOrigins.map(origin => `${origin.label}, ${origin.activeVoices ? `${origin.activeVoices} sounding` : "sound fading"}`).join("; ")}</span>
    </div>
  );
}
