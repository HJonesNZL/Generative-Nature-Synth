import { StrictMode } from "react";
import { createRoot } from "react-dom/client";
import { App } from "./app/App";
import { NativeAtlasHost } from "./host/nativeHost";
import "./styles/fonts.css";
import "./styles/tokens.css";
import "./styles/global.css";
import "./styles/app.css";

createRoot(document.getElementById("root")!).render(
  <StrictMode><App host={new NativeAtlasHost()} /></StrictMode>,
);
