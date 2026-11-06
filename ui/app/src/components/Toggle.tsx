/** On/off switch with an explicit text state next to it (not colour-only). */
export function Toggle({ label, checked, onChange, disabled, showState = true }: {
  label: string;
  checked: boolean;
  onChange: (next: boolean) => void;
  disabled?: boolean;
  showState?: boolean;
}) {
  return (
    <button type="button" role="switch" aria-checked={checked} aria-label={label} disabled={disabled} className="toggle" onClick={() => onChange(!checked)}>
      <span className="toggle__track"><span className="toggle__thumb" /></span>
      {showState ? <span className="toggle__state" aria-hidden="true">{checked ? "On" : "Off"}</span> : null}
    </button>
  );
}
