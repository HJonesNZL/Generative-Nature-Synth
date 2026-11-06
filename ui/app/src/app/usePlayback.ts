/** Saved-piece preparation and polling live above both screens. C++ owns all audio/time. */
import { useEffect, useRef } from "react";
import { playbackContext } from "./playbackContext";
import { useHost } from "./HostContext";
import { useAppState, useDispatch } from "./store";

export function usePlayback() {
  const { client, caps, connection } = useHost();
  const state = useAppState();
  const dispatch = useDispatch();
  const latest = useRef(state.transport);
  latest.current = state.transport;
  const sessionId = state.replay.session?.id;
  const context = playbackContext(state.mode, sessionId, state.timeBasis, state.replay.speedNumerator, state.replay.speedDenominator, state.replay.preparationRevision);
  const adoptedContext = useRef<string | null>(null);
  useEffect(() => {
    if (!caps || connection !== "connected") return;
    // Recall already prepared a quiet native scene. Adopt it once, without a
    // second preparation that might restore stale context or erase its draft.
    if (state.transport.preparationId && state.transport.preparedContext === context && adoptedContext.current !== context) {
      adoptedContext.current = context;
      return;
    }
    const controller = new AbortController();
    dispatch({ type: "transport", transport: { preparationId: null, state: "stopped", positionUs: "0", durationUs: null, error: null, preparing: state.mode === "replay" && !!sessionId } });
    void (async () => {
      // Safety mutations are never cancelled after submission: an aborted HTTP request
      // cannot undo audio already started by the host. Prepare only after quiet is acknowledged.
      if (client.supports("setTransport")) {
        const stopped = await client.call("setTransport", { action: "panic" });
        if (controller.signal.aborted) return;
        if (!stopped.ok) {
          if (client.supports("setAudioEnabled")) await client.call("setAudioEnabled", { enabled: false });
          if (!controller.signal.aborted) dispatch({ type: "transport", transport: { error: stopped.error.message, preparing: false } });
          return;
        }
      }
      if (caps.implementation === "native" && client.supports("setAudioEnabled")) {
        const disabled = await client.call("setAudioEnabled", { enabled: false });
        if (controller.signal.aborted) return;
        if (!disabled.ok) { dispatch({ type: "transport", transport: { error: disabled.error.message, preparing: false } }); return; }
      }
      if (state.mode !== "replay" || !sessionId || !client.supports("prepareReplay")) return;
      const prepared = await client.call("prepareReplay", { sessionId, basis: state.timeBasis, speedNumerator: state.replay.speedNumerator, speedDenominator: state.replay.speedDenominator }, { signal: controller.signal });
      if (controller.signal.aborted) return;
      if (!prepared.ok) { dispatch({ type: "transport", transport: { error: prepared.error.message, preparing: false } }); return; }
      dispatch({ type: "transport", transport: { preparationId: prepared.value.preparationId, generation: prepared.value.generation, durationUs: prepared.value.durationUs, positionUs: "0", state: "stopped", preparing: false } });
    })();
    return () => controller.abort();
  }, [client, caps, connection, context]);

  useEffect(() => {
    if (!caps || connection !== "connected" || !client.supports("getTransport")) return;
    let cancelled = false;
    let timer = 0;
    const controller = new AbortController();
    const poll = async () => {
      const expected = latest.current;
      const revision = client.playbackRevision;
      const result = await client.call("getTransport", {}, { signal: controller.signal, timeoutMs: 2500 });
      if (cancelled) return;
      if (revision !== client.playbackRevision) { timer = window.setTimeout(() => void poll(), 500); return; }
      if (!result.ok) dispatch({ type: "transportAck", preparationId: expected.preparationId, generation: expected.generation, transport: { error: result.error.message } });
      else if (expected.preparationId) {
        const value = result.value;
        if (value.preparationId === expected.preparationId && BigInt(value.generation) >= BigInt(expected.generation)) {
          dispatch({ type: "transportAck", preparationId: expected.preparationId, generation: expected.generation, transport: { ...value, error: null } });
        } else if (value.preparationId !== expected.preparationId && latest.current.preparationId === expected.preparationId) {
          dispatch({ type: "transportAck", preparationId: expected.preparationId, generation: expected.generation, transport: { preparationId: null, state: "stopped", error: "Playback preparation changed. Select the saved piece again." } });
        }
      }
      timer = window.setTimeout(() => void poll(), 500);
    };
    void poll();
    return () => { cancelled = true; controller.abort(); window.clearTimeout(timer); };
  }, [client, caps, connection]);

  useEffect(() => {
    const quiet = () => {
      if (client.supports("setAudioEnabled")) void client.call("setAudioEnabled", { enabled: false });
      if (client.supports("setTransport")) void client.call("setTransport", { action: "panic" });
    };
    window.addEventListener("pagehide", quiet);
    return () => { window.removeEventListener("pagehide", quiet); quiet(); };
  }, [client]);
}
