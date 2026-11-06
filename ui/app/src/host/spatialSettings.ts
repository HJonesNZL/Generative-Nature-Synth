import type { SpatialRegionSettings, SpatialSettings } from "./contracts";

export const spatialNumbers = [
  { key: "south", label: "South latitude", min: -90, max: 90, step: .1 },
  { key: "north", label: "North latitude", min: -90, max: 90, step: .1 },
  { key: "west", label: "West longitude", min: -180, max: 180, step: .1 },
  { key: "east", label: "East longitude", min: -180, max: 180, step: .1 },
  { key: "pan", label: "Pan", min: -1, max: 1, step: .05 },
  { key: "gainDb", label: "Gain dB", min: -60, max: 0, step: 1 },
  { key: "send", label: "Ambience send", min: 0, max: 1, step: .05 },
  { key: "echoMix", label: "Echo / room blend", min: 0, max: 1, step: .05 },
  { key: "delayMs", label: "Echo delay ms", min: 20, max: 500, step: 1 },
  { key: "feedback", label: "Echo feedback", min: 0, max: .5, step: .01 },
  { key: "roomSeconds", label: "Nominal room decay s", min: .2, max: 2.5, step: .1 },
  { key: "tone", label: "Room tone", min: 0, max: 1, step: .05 },
] as const;
export type SpatialNumber = typeof spatialNumbers[number]["key"];
const bounded = (v: unknown, min: number, max: number) => typeof v === "number" && Number.isFinite(v) && v >= min && v <= max;
const validName = (value: unknown) => typeof value === "string" && value.length > 0 && new TextEncoder().encode(value).length <= 128 &&
  [...value].every(character => { const cp = character.codePointAt(0)!; return cp >= 32 && !(cp >= 127 && cp <= 159) && !(cp >= 0xd800 && cp <= 0xdfff); });
export function spatialSettingsError(value: unknown): string | null {
  if (!value || typeof value !== "object" || Array.isArray(value)) return "Spatial settings must be an object.";
  const settings = value as Record<string, unknown>;
  if (Object.keys(settings).length !== 3 || typeof settings.enabled !== "boolean" || !bounded(settings.amount, 0, 1) || !Array.isArray(settings.regions) || settings.regions.length > 8) return "Use enabled, amount from 0 to 1 and up to eight regions.";
  const ids = new Set();
  for (const [index, item] of settings.regions.entries()) {
    if (!item || typeof item !== "object" || Array.isArray(item)) return `Region ${index + 1} must be an object.`;
    const region = item as Record<string, unknown>;
    if (Object.keys(region).length !== 15 || typeof region.id !== "string" || !/^[A-Za-z0-9_-]{1,64}$/.test(region.id) || ids.has(region.id)) return `Region ${index + 1} needs a unique ID of 1–64 letters, digits, underscores or hyphens.`;
    ids.add(region.id);
    if (!validName(region.name) || typeof region.positionPan !== "boolean") return `Region ${index + 1} needs a valid name of 1–128 UTF-8 bytes and a position-pan switch.`;
    for (const spec of spatialNumbers) if (!bounded(region[spec.key], spec.min, spec.max)) return `${region.name}: ${spec.label} must be ${spec.min} to ${spec.max}.`;
    if ((region.south as number) >= (region.north as number)) return `${region.name}: south must be below north.`;
    if (region.west === region.east || region.west === 180 && region.east === -180) return `${region.name}: longitude bounds must enclose a nonzero area.`;
  }
  return null;
}
export const validSpatialSettings = (value: unknown): value is SpatialSettings => spatialSettingsError(value) === null;
export const newSpatialRegion = (id: string, name: string): SpatialRegionSettings => ({ id, name, south: -90, north: 90, west: -180, east: 180, pan: 0, gainDb: 0, send: .25, echoMix: .3, delayMs: 180, feedback: .25, roomSeconds: 1.2, tone: .5, positionPan: false });

export type SpatialRegionDraft = Omit<SpatialRegionSettings, SpatialNumber> & Record<SpatialNumber, string>;
export interface SpatialDraft { enabled: boolean; amount: string; regions: SpatialRegionDraft[] }
export function spatialDraft(settings: SpatialSettings): SpatialDraft {
  return { enabled: settings.enabled, amount: String(settings.amount), regions: settings.regions.map(region => {
    const draft = { ...region } as unknown as SpatialRegionDraft;
    for (const { key } of spatialNumbers) draft[key] = String(region[key]);
    return draft;
  }) };
}
export function spatialFromDraft(draft: SpatialDraft): SpatialSettings {
  const number = (text: string, label: string) => {
    if (!text.trim() || !/^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:e[+-]?\d+)?$/i.test(text)) throw Error(`${label}: enter a complete number before applying or saving.`);
    return Number(text);
  };
  const settings: SpatialSettings = { enabled: draft.enabled, amount: number(draft.amount, "Global ambience amount"), regions: draft.regions.map(region => {
    const result = { ...region } as unknown as SpatialRegionSettings;
    for (const { key, label } of spatialNumbers) result[key] = number(region[key], `${region.name || "Region"}: ${label}`);
    return result;
  }) };
  const error = spatialSettingsError(settings); if (error) throw Error(error);
  return settings;
}
