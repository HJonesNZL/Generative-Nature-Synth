import type { AudioDeviceCatalog } from "./contracts";

const text = (value: unknown, maximum = 256): value is string => typeof value === "string" && value.length > 0 && value.length <= maximum;
const integer = (value: unknown, minimum: number, maximum: number): value is number => Number.isInteger(value) && (value as number) >= minimum && (value as number) <= maximum;
const revision = (value: unknown): value is string => typeof value === "string" && /^(0|[1-9][0-9]{0,18})$/.test(value) && BigInt(value) <= 9223372036854775807n;
const rate = (value: unknown): value is number => typeof value === "number" && Number.isFinite(value) && value >= 8000 && value <= 768000;
const frames = (value: unknown) => integer(value, 1, 65536);
const boundedList = (value: unknown, valid: (value: unknown) => boolean) => Array.isArray(value) && value.length <= 64 && value.every(valid) && new Set(value).size === value.length;

/** Reject malformed or unbounded native catalogs before they reach controls. */
export function validAudioDeviceCatalog(value: unknown): value is AudioDeviceCatalog {
  if (!value || typeof value !== "object") return false;
  const catalog = value as Record<string, unknown>;
  const configuration = catalog.configuration as Record<string, unknown> | null;
  if (typeof catalog.available !== "boolean" || !revision(catalog.controlRevision) || !revision(catalog.generation) ||
      !(catalog.preparationId === null || text(catalog.preparationId, 256)) || !(catalog.error === null || typeof catalog.error === "string" && catalog.error.length <= 4096) ||
      !configuration || typeof configuration !== "object" || !["systemDefault", "explicitDevice"].includes(configuration.mode as string) ||
      !(configuration.mode === "systemDefault" ? configuration.deviceId === null : text(configuration.deviceId, 64)) ||
      !integer(configuration.bufferFrames, 0, 65536) || !Array.isArray(catalog.devices) || catalog.devices.length > 128) return false;
  const ids = new Set<string>();
  if (!catalog.devices.every((device: Record<string, unknown>) => {
    if (!device || !text(device.id, 64) || ids.has(device.id)) return false;
    ids.add(device.id);
    return text(device.name) && text(device.type, 256) && typeof device.isDefault === "boolean" && integer(device.outputChannels, 0, 65536) &&
      boundedList(device.sampleRates, rate) && boundedList(device.bufferFrames, frames) && integer(device.defaultBufferFrames, 0, 65536);
  })) return false;
  if (catalog.active === null) return true;
  if (!catalog.active || typeof catalog.active !== "object") return false;
  const active = catalog.active as Record<string, unknown>;
  return text(active.deviceId, 64) && text(active.name) && text(active.type, 256) && rate(active.sampleRate) && frames(active.bufferFrames) && integer(active.outputChannels, 1, 64);
}
