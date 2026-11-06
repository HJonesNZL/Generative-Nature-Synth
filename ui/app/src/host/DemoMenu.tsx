/**
 * DEMO-ONLY menu for the mock host: switch deterministic failure/empty
 * scenarios and simulate host automation. main.tsx passes it to the shell only
 * when the mock adapter is in use; a native build never renders or bundles it
 * if main.native.tsx (see README) is used as the entry.
 */
import { Icon } from "../components/Icon";
import { Popover } from "../components/Popover";
import { useAppState, useDispatch } from "../app/store";
import { MOCK_SCENARIOS, type MockAtlasHost } from "./mockHost";

export function DemoMenu({ mock }: { mock: MockAtlasHost }) {
  const state = useAppState();
  const dispatch = useDispatch();
  return (
    <Popover label="Demo scenarios (mock host only)" trigger={<span className="pill pill--quiet" title="Demo states"><Icon name="demo" size={14} /><span className="sr-only">Demo states</span></span>}>
      {(close) => (
        <div className="menu menu--wide">
          <p className="menu__title">Mock host scenarios</p>
          <p className="menu__note">Demo only. Switches the deterministic mock between truthful states.</p>
          {MOCK_SCENARIOS.map((s) => (
            <button key={s.id} type="button" className={`menu__item${mock.getScenario() === s.id ? " is-current" : ""}`} aria-pressed={mock.getScenario() === s.id} onClick={() => { mock.setScenario(s.id); close(); }}>
              {s.label}
            </button>
          ))}
          <hr />
          <button type="button" className="menu__item" disabled={!state.selectedVoiceId} onClick={() => { if (state.selectedVoiceId) mock.simulateAutomation(state.selectedVoiceId, "tone"); close(); }}>
            Simulate host automation (Tone of selected voice)
          </button>
          <label className="menu__check"><input type="checkbox" checked={state.reducedMotion} onChange={(e) => dispatch({ type: "reducedMotion", value: e.target.checked })} /> Reduce motion</label>
        </div>
      )}
    </Popover>
  );
}
