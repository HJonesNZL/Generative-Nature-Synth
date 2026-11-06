# Asset and dependency licences

Everything the built UI needs is bundled and free to redistribute. Nothing requires payment, an account, a subscription, a CDN or network access.

## Fonts (bundled in `src/assets/fonts/`)

| Font | Files | Origin | Licence | Modifications |
|---|---|---|---|---|
| Unbounded 500, 700 | `unbounded-latin[-ext]-{500,700}-normal.woff2` | The Unbounded Project Authors (github.com/googlefonts/unbounded), packaged by `@fontsource/unbounded@5.3.0` | SIL OFL 1.1, `OFL-Unbounded.txt` | Fontsource Latin / Latin Extended subsets, unmodified |
| Red Hat Text 400, 400 italic, 500, 600 | `red-hat-text-latin[-ext]-*.woff2` | Red Hat, Inc., via `@fontsource/red-hat-text@5.3.0` | SIL OFL 1.1, `OFL-RedHatText.txt` | subsets, unmodified |
| Red Hat Mono 400, 500 | `red-hat-mono-latin[-ext]-*.woff2` | Red Hat, Inc., via `@fontsource/red-hat-mono@5.3.0` | SIL OFL 1.1, `OFL-RedHatMono.txt` | subsets, unmodified |

The OFL allows bundling and redistribution with software. The licence text must accompany the fonts, which the `OFL-*.txt` files do. The reserved font names must not be used for modified versions; these are unmodified.

Fallback stacks (no files shipped): Arial Black / Helvetica Neue / Arial; system UI; ui-monospace / Menlo / Consolas.

## Geography (bundled in `src/assets/geo/`)

| File | Origin | Licence | Modifications |
|---|---|---|---|
| `land-110m.json` | Natural Earth 1:110m land, via `world-atlas@2.0.2` (TopoJSON by Mike Bostock) | Natural Earth: **public domain**. world-atlas package: ISC. | Decoded from TopoJSON to plain `[lon, lat]` rings and rounded to 0.01°. Rings that cross the antimeridian are longitude-unwrapped; pole-encircling rings are closed along the pole. |
| `nz-50m.json` | Natural Earth 1:50m land, via `world-atlas@2.0.2` | public domain / ISC | Clipped to 165.5–179.5°E, 48–33.8°S; simplified with 0.01° tolerance |

This is real cartographic data. Station positions, region outlines and satellite fixes in the **mock** are illustrative and are not survey data.

## Original assets made for this project

| Asset | Notes | Licence |
|---|---|---|
| `src/assets/icons/*.svg` (29 icons) | Drawn for Animal Atlas: 16 × 16 grid, 1.5 px stroke, `currentColor` | Owner's project licence (no third-party source) |
| `src/assets/textures/grain-light.png` | Procedurally generated 220 × 220 noise tile (seeded random alpha dots) | Owner's project licence |
| Globe land texture | Painted at runtime from the Natural Earth rings (`landTexture.ts`); no raster shipped | — |

No reference-image branding, logos, photographs or generated map imagery are included. The design references informed only the style.

## Runtime dependencies (shipped in `dist/`)

| Package | Version | Licence |
|---|---|---|
| react | 19.3.1 | MIT |
| react-dom | 19.3.1 | MIT |
| scheduler (via react-dom) | 0.28.0 | MIT |
| three | 0.187.0 | MIT (includes `examples/jsm/controls/OrbitControls.js`) |

Keep their MIT notices with a distributed binary. Vite preserves licence comments in `dist/assets/*.js`. A plain `THIRD_PARTY_NOTICES` file is recommended for the JUCE app bundle.

## Build-time only (not shipped)

| Package | Licence |
|---|---|
| vite 8.3.5, @vitejs/plugin-react 6.2.1, @types/react, @types/react-dom, @types/three | MIT |
| typescript 5.9.3, detect-libc, @dimforge/rapier3d-compat (types dependency of @types/three) | Apache-2.0 |
| lightningcss 1.33.0 (Vite CSS minifier) | MPL-2.0 (build tool only; no source is included in the output) |
| picocolors | ISC |
| source-map-js | BSD-3-Clause |

The full list is in `package-lock.json`.

## Unresolved / flagged

- **JUCE licence:** whether the native shell uses AGPL or commercial JUCE is outside this package and still undecided (see docs/native-react-architecture.md in the main repo).
- **Data reuse permissions:** BirdWeather and NZ Great White reuse permissions are tracked separately in the source registry. The UI shows `permissionsStatus` from the host but does not decide it.
