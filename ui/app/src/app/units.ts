/** Normalized ⇄ display-unit conversion for host-described parameters. */
import type { VoiceParameterSpec } from "../host/provisional";

export function toNormalized(spec: VoiceParameterSpec, value: number): number {
  const n = spec.curve === "log" ? Math.log(value / spec.min) / Math.log(spec.max / spec.min) : (value - spec.min) / (spec.max - spec.min);
  return Math.min(1, Math.max(0, n));
}
export function fromNormalized(spec: VoiceParameterSpec, normalized: number): number {
  const n = Math.min(1, Math.max(0, normalized));
  return spec.curve === "log" ? spec.min * Math.pow(spec.max / spec.min, n) : spec.min + n * (spec.max - spec.min);
}
export function formatParameter(spec: VoiceParameterSpec, normalized: number): string {
  const v = fromNormalized(spec, normalized);
  switch (spec.unit) {
    case "st": return `${v >= 0 ? "+" : ""}${v.toFixed(spec.step === 1 ? 0 : 1)} st`;
    case "s": return v < 1 ? `${Math.round(v * 1000)} ms` : `${v.toFixed(2)} s`;
    case "Hz": return `${v.toFixed(1)} Hz`;
    case "dB": return `${v.toFixed(1)} dB`;
    case "pan": return Math.abs(v) < 0.005 ? "C" : `${v < 0 ? "L" : "R"}${Math.round(Math.abs(v) * 50)}`;
    case "%": return `${Math.round(v)} %`;
    default: return v.toFixed(2);
  }
}
/** Accepts the displayed form ("120 ms", "0.4 s", "-6 dB", "L12", "+3") and returns normalized or null. */
export function parseParameter(spec: VoiceParameterSpec, text: string): number | null {
  const t = text.trim().toLowerCase();
  let v: number;
  if (spec.unit === "pan") {
    if (t === "c" || t === "0") v = 0;
    else {
      const m = /^([lr])\s*(\d+(\.\d+)?)$/.exec(t);
      if (!m) return null;
      v = (Number(m[2]) / 50) * (m[1] === "l" ? -1 : 1);
    }
  } else {
    const m = /^([+-]?\d+(\.\d+)?)\s*(ms|s|st|db|%|hz)?$/.exec(t);
    if (!m) return null;
    v = Number(m[1]);
    if (m[3] === "ms") v /= 1000;
  }
  if (!Number.isFinite(v) || v < spec.min || v > spec.max) return null;
  return toNormalized(spec, v);
}
