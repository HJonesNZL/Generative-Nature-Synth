/** Inline SVG icons loaded from editable files in src/assets/icons (currentColor). */
const files = import.meta.glob("../assets/icons/*.svg", { query: "?raw", import: "default", eager: true }) as Record<string, string>;
const icons: Record<string, string> = {};
for (const [path, svg] of Object.entries(files)) icons[path.replace(/^.*\/(.+)\.svg$/, "$1")] = svg;

export type IconName =
  | "play" | "pause" | "stop" | "collect" | "panic" | "loop" | "to-start" | "globe" | "map" | "plus" | "minus" | "recenter"
  | "chevron-left" | "chevron-right" | "chevron-down" | "studio" | "undo" | "redo" | "warning" | "info" | "check" | "close"
  | "station" | "satellite-fix" | "speaker" | "speaker-off" | "bookmark" | "render" | "demo";

export function Icon({ name, size = 16, className }: { name: IconName; size?: number; className?: string }) {
  const svg = icons[name] ?? "";
  return (
    <span
      className={`icon${className ? ` ${className}` : ""}`}
      style={{ width: size, height: size }}
      aria-hidden="true"
      // Trusted, build-time local SVG files only — never host/provider text.
      dangerouslySetInnerHTML={{ __html: svg.replace(/width="16" height="16"/, `width="${size}" height="${size}"`) }}
    />
  );
}
