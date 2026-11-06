/**
 * Browser/demo entry. Injects the deterministic MOCK host and the demo-only
 * scenario menu. For the JUCE build, create main.native.tsx that injects the
 * native adapter instead (see README → "Connecting the native host").
 */
import { StrictMode } from "react";
import { createRoot } from "react-dom/client";
import { App } from "./app/App";
import { MockAtlasHost } from "./host/mockHost";
import { DemoMenu } from "./host/DemoMenu";
import "./styles/fonts.css";
import "./styles/tokens.css";
import "./styles/global.css";
import "./styles/app.css";

const host = new MockAtlasHost();
createRoot(document.getElementById("root")!).render(
  <StrictMode>
    <App host={host} demoMenu={() => <DemoMenu mock={host} />} />
  </StrictMode>,
);
