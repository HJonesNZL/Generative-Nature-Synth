/**
 * Application shell: one injected host, shared state, header, global status
 * banners and the two screens. The Atlas is unmounted while Sound Studio is
 * shown (freeing the WebGL context); all shared state lives above it.
 */
import type { ReactNode } from "react";
import type { ExtendedHost } from "../host/client";
import type { AtlasHost } from "../host/contracts";
import { AtlasScreen } from "../screens/atlas/AtlasScreen";
import { SoundStudioScreen } from "../screens/studio/SoundStudioScreen";
import { LiveActivityProvider } from "./LiveActivityContext";
import { LiveSpeciesCountsProvider } from "./LiveSpeciesCountsContext";
import { LiveCollectionProvider } from "./LiveCollectionContext";
import { LiveSettingsProvider } from "./LiveSettingsContext";
import { SpatialProvider } from "./SpatialContext";
import { ProjectTools } from "./ProjectTools";
import { Header } from "./Header";
import { usePlayback } from "./usePlayback";
import { HostProvider, useHost, useHostQuery, valueOf } from "./HostContext";
import { AppStateProvider, useAppState } from "./store";
import { useLiveFeed, useMaterialFeed } from "./useFeeds";
import { Notice } from "../components/Notice";
import { useEffect } from "react";

export function App({ host, demoMenu }: { host: ExtendedHost | AtlasHost; demoMenu?: (props: Record<string, never>) => ReactNode }) {
  return (
    <HostProvider host={host}>
      <AppStateProvider demo={Boolean("isMock" in host && host.isMock)}>
        <LiveCollectionProvider><LiveSpeciesCountsProvider><LiveSettingsProvider><SpatialProvider><LiveActivityProvider><Shell demoMenu={demoMenu} /></LiveActivityProvider></SpatialProvider></LiveSettingsProvider></LiveSpeciesCountsProvider></LiveCollectionProvider>
      </AppStateProvider>
    </HostProvider>
  );
}

function Shell({ demoMenu }: { demoMenu?: (props: Record<string, never>) => ReactNode }) {
  const state = useAppState();
  usePlayback();
  useLiveFeed();
  useMaterialFeed();
  useEffect(() => {
    document.documentElement.dataset.reducedMotion = String(state.reducedMotion);
  }, [state.reducedMotion]);
  return (
    <div className="app">
      <a className="skip-link" href="#main">Skip to content</a>
      <div inert={state.soundscape.operation !== null}><Header demoMenu={demoMenu ? demoMenu({}) : null} projectTools={<ProjectTools />} /><StatusBanners /></div>
      <div className="app-workspace" inert={state.soundscape.operation !== null}>{state.screen === "atlas" ? <AtlasScreen /> : <SoundStudioScreen />}</div>
    </div>
  );
}

function StatusBanners() {
  const { connection, connectionError, caps, reconnect, onNotification } = useHost();
  const [audioQ, refreshAudio] = useHostQuery("getAudioState", {}, [], { refreshMs: 1000 });
  useEffect(() => onNotification((n) => { if (n.type === "audioStateChanged") refreshAudio(); }), [onNotification, refreshAudio]);
  const audio = valueOf(audioQ);
  return (
    <div className="banners" aria-live="polite">
      {connection === "connecting" ? <Notice tone="info" title="Connecting to the host…" /> : null}
      {connection === "reconnecting" ? <Notice tone="caution" title="Reconnecting to the host" live>{connectionError ?? "The bridge dropped."} Controls that change the piece are paused; nothing will be replayed when it returns.</Notice> : null}
      {connection === "failed" ? <Notice tone="problem" title="Host unavailable" action={<button type="button" className="btn btn--quiet btn--sm" onClick={reconnect}>Retry</button>}>{connectionError}</Notice> : null}
      {audio?.status === "unavailable" ? <Notice tone="problem" title="Audio unavailable">No output device. Collection and the map keep working; nothing will sound.</Notice> : null}
      {audio?.status === "device_lost" ? <Notice tone="problem" title="Audio device lost">Output stopped and every note was released. {caps?.supportedMethods.includes("getAudioDevices") ? "Open Audio settings, refresh and apply an available output. Enable Listening and press Play when ready." : "Reconnect a device in the host’s audio settings."}</Notice> : null}
      {caps && caps.implementation === "mock" ? <span className="sr-only">Demo host: all data is illustrative and no audio is produced.</span> : null}
    </div>
  );
}
