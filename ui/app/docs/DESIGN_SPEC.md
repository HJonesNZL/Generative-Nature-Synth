# Design specification — Direction H2 · Region Mix

The source of truth for values is `src/styles/tokens.css`. The tables below mirror it. Change the token, not the component CSS.

## Design principles carried from the reviews

| Principle | From | How it shows up |
|---|---|---|
| One continuous ground; no filled panels behind menus or text | Concept B | Cream paper ground with grain across the whole window. Areas are separated only by rules (hairline, strong, 3 px double) and spacing. |
| Three-column layout with a bottom band | Concept A | Left: species/sources, history query or saved pieces. Centre: globe + NZ loupe. Right: Region Mix + inspector. Bottom: Grain. |
| World view plus species highlighted *now* | Concept C | The highlighted species' stations are filled green on the globe and loupe. Other stations are hollow; stations with nothing in the window are dashed. |
| What animals contributed to a place over time | Concept E | Region Mix (share per species in the region, with voice, solo and mute) and Grain (one line per detection per species). |
| Woodgrain / growth-ring texture | Reference poster | Hatched land on the globe; grain bars in Region Mix. |

## Colour tokens

| Token | Value | Use | Contrast on ground |
|---|---|---|---|
| `--c-ground` | `#eeeadf` | page ground (with `grain-light.png` overlay) | — |
| `--c-ground-sunk` | `rgba(31,90,50,.06)` | hover, highlighted row, lane background | — |
| `--c-ground-raised` | `#f6f3ea` | popovers, inline numeric entry | — |
| `--c-ink` | `#1b2a1f` | body text | 12.5:1 |
| `--c-ink-muted` | `#4e5e52` | secondary text, labels | 5.7:1 (5.2:1 on sunk rows) |
| `--c-ink-faint` | `#5c695f` | disabled-looking mapping rows (still ≥ 4.5:1) | 4.8:1 |
| `--c-accent` | `#1f5a32` | forest ink: headings, controls, highlight | 6.8:1 (6.2:1 on sunk rows) |
| `--c-accent-strong` | `#123a20` | hover on filled accent | — |
| `--c-on-accent` | `#eeeadf` | text on accent fills | 6.8:1 |
| `--c-rule` | `#c9c3b1` | decorative hairlines (never the only cue) | — |
| `--c-warn` | `#a8361a` | problems, missing voice, render failure, selected-event tick | 5.5:1 |
| `--c-caution` | `#7a5a00` | stale / partial / truncated | 5.3:1 |
| `--c-focus` | `#1a4fa0` | focus ring only | 6.5:1 |
| `--c-sp-1…6` | greens + `#3d5a6c` | Grain lanes and swatches; always paired with a name | — |

Contrast ratios are calculated for flat `#eeeadf`. The grain overlay is sparse black dots of at most 17% alpha (mostly under 5%), so local contrast varies slightly.

There is no dark theme. The direction is a printed-paper aesthetic. A dark variant is an open decision.

## Typography

| Role | Font | Weight | Size (rem / px at 16 px root) |
|---|---|---|---|
| Display (inspector species, voice title) | Unbounded | 700 | `--fs-display` 1.75 / 28 (24 in inspector) |
| Section heading (region name, Grain title, group) | Unbounded | 500 | `--fs-h3` 1 / 16 |
| UI text | Red Hat Text | 400/500/600 | `--fs-ui` 14, `--fs-body` 13, `--fs-small` 12 |
| Labels, numbers, times | Red Hat Mono | 400/500 | `--fs-label` 11 (uppercase, +0.04em) |

- **Fallback stacks** are in `tokens.css`: Arial Black/Helvetica for display, system UI for text, ui-monospace/Menlo/Consolas for mono.
- **Character coverage:** only the Latin and Latin Extended subsets are shipped, which covers te reo Māori macrons.
- **Text sizing:** all text uses `rem`, so raising the root size enlarges it (see `previews/atlas-text-125pct-1440x900.png`).

## Spacing, shape, elevation

- **Spacing scale:** 4, 6, 8, 10, 14, 18, 24, 32 px (`--s-1…8`). The window gutter is 32 px, or 24 px below 1440 px wide.
- **Shapes:** pill radius 999 px for buttons, chips, inputs and segmented controls; 6 px for small elements.
- **Rules:** hairline `1px solid --c-rule`; strong `1px solid --c-accent`; double `3px double --c-accent`. The double rule is used under the header, above Grain and above the event panel.
- **Shadows:** popovers only (`0 10px 28px rgba(27,42,31,.18)`). The loupe uses a 3 px double accent border instead of a shadow.

## Layout dimensions

| Area | Size |
|---|---|
| Header | min 64 px, wraps to a second row below ≈1340 px wide |
| Left column | 240 px (220 px below 1440 px wide) |
| Right column | 300 px (288 px below 1440 px wide) |
| Centre | `minmax(0, 1fr)`; globe fills it; loupe `clamp(180px, 34%, 250px)` bottom-right |
| Bottom (Grain) | 196 px fixed; left info column matches the left column width |
| Grain lane | 22 px high, 82 px name column, up to 6 lanes |
| Region Mix row | 48 px; columns 82 px / bar / 34 px / 54 px |

- **Smallest usable window:** 1280 × 800 (see previews). Below that, columns compress without horizontal scrolling (1100 px was checked), but the header wraps and the centre becomes cramped.
- **Overflow:** side columns and the Studio centre scroll vertically, independently. The page never scrolls.

## Controls

| Control | Size | Notes |
|---|---|---|
| Button | 38 px (32 small) | outline, primary (filled), quiet (underlined text), warn |
| Icon button | 38 / 32 px round | primary variant filled |
| Pill / chip | 32 / 28 px | `is-on` / `is-current` filled |
| Round S/M toggle | 24 × 24 px | WCAG 2.2 minimum target |
| Segmented | 28 px options (24 small) | radio semantics, arrow keys |
| Toggle switch | 34 × 18 px track, 24 px hit area | text On/Off beside it |
| Knob | 52 px dial; 270° sweep from −135° | value button below; • marks an override |
| Icons | 12–16 px, 1.5 px stroke, `currentColor` | `src/assets/icons/*.svg` |

## Map rendering

**Globe**
- Three.js sphere using an equirectangular canvas texture painted from Natural Earth 110m rings, with New Zealand from the 50m rings.
- Cream ocean, hatched land ("woodgrain"), green coastlines, 15° graticule, and a back-side rim sphere for the outline.
- Transparent clear colour, so the paper ground continues behind it.

**Markers**
- Instanced discs and rings for stations, diamonds for satellite fixes.
- A 2.2× ring marks the selection, with a mono label beside it.

**Loupe and 2D map**
- The same SVG renderer (`Map2D`).
- Dashed region outlines; the selected region has a stronger fill.
- Uncertainty circles are drawn for fixes that report a radius.

The geography is real cartographic data. Station positions in the demo are illustrative.

## Motion

| Token | Value | Use |
|---|---|---|
| `--dur-fast` | 120 ms | hover and toggle transitions |
| `--dur-base` | 200 ms | reserved |
| `--dur-pulse` / globe | 1200 ms | arrival ring on the globe (max 24 at once) |

- The globe renders at ≤ 30 fps, only while something changes. Pixel ratio is capped at 2. Orbit damping is on unless motion is reduced.
- Reduced motion is set by the OS `prefers-reduced-motion` setting or the in-app "Reduce motion" toggle. It sets every duration to 0, removes arrival pulses and disables orbit damping. There is no other animation in the UI.

## Open design decisions

- Sound Studio layout is a first pass and has not been reviewed on the canvas.
- Final macro names, voice types and parameter ranges are pending DSP review.
- Dark theme: none designed.
- Compact VST3 editor: not designed (see INTERACTIONS_AND_STATES.md §8).
- Source-setup drawer and region drawing tool: shown disabled, not designed.
- Whether Region Mix percentages count *detections* (current) or *sounded gestures*.
