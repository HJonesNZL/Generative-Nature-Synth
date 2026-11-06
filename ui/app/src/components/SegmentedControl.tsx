import { useRef, type KeyboardEvent } from "react";

/** Single-choice control with radio semantics and arrow-key movement. */
export function SegmentedControl<T extends string>({ label, value, options, onChange, size = "md", disabledValues = [] }: {
  label: string;
  value: T;
  options: Array<{ value: T; label: string; hint?: string }>;
  onChange: (value: T) => void;
  size?: "sm" | "md";
  disabledValues?: T[];
}) {
  const refs = useRef<Array<HTMLButtonElement | null>>([]);
  const enabled = options.filter((o) => !disabledValues.includes(o.value));
  const onKeyDown = (event: KeyboardEvent) => {
    const delta = event.key === "ArrowRight" || event.key === "ArrowDown" ? 1 : event.key === "ArrowLeft" || event.key === "ArrowUp" ? -1 : 0;
    if (!delta) return;
    event.preventDefault();
    const index = enabled.findIndex((o) => o.value === value);
    const next = enabled[(index + delta + enabled.length) % enabled.length];
    if (!next) return;
    onChange(next.value);
    refs.current[options.indexOf(next)]?.focus();
  };
  return (
    <div className={`segmented segmented--${size}`} role="radiogroup" aria-label={label} onKeyDown={onKeyDown}>
      {options.map((o, i) => {
        const checked = o.value === value;
        const disabled = disabledValues.includes(o.value);
        return (
          <button
            key={o.value}
            ref={(el) => { refs.current[i] = el; }}
            type="button"
            role="radio"
            aria-checked={checked}
            tabIndex={checked ? 0 : -1}
            disabled={disabled}
            title={o.hint}
            className="segmented__option"
            onClick={() => onChange(o.value)}
          >
            {o.label}
          </button>
        );
      })}
    </div>
  );
}
