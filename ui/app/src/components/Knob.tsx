/**
 * Rotary control (role="slider").
 *
 * Pointer: drag vertically (200 px = full range); hold Shift for fine (×0.1).
 * Keyboard: ←/↓ −1 %, →/↑ +1 %, Shift = 0.1 %, PageUp/PageDown ±10 %,
 *   Home/End = min/max, Enter = type a value, Delete/Backspace = reset.
 * Double-click resets to default. Every edit is reported as begin → change* → end.
 * While the user holds a gesture, external (host/automation) values are not
 * applied, so the control never fights the user or echoes a value back.
 */
import { useEffect, useId, useRef, useState, type KeyboardEvent, type PointerEvent } from "react";

export type Gesture = "begin" | "change" | "end";

export interface KnobProps {
  label: string;
  value: number;                 // normalized 0..1 from the authoritative host state
  defaultValue: number;          // normalized
  format: (normalized: number) => string;
  parse: (text: string) => number | null; // display units → normalized, null if invalid
  onGesture: (gesture: Gesture, normalized: number) => void;
  disabled?: boolean;
  size?: number;
  overridden?: boolean;          // value differs from inherited parent
  normalizedStep?: number;     // optional host-described discrete step
  pending?: boolean;             // awaiting host acknowledgement
}

const clamp = (n: number) => Math.min(1, Math.max(0, n));
const START = -135;
const SWEEP = 270;

export function Knob({ label, value, defaultValue, format, parse, onGesture, disabled, size = 52, overridden, pending, normalizedStep }: KnobProps) {
  const [local, setLocal] = useState<number | null>(null);
  const [editing, setEditing] = useState(false);
  const [draft, setDraft] = useState("");
  const [error, setError] = useState<string | null>(null);
  const drag = useRef<{ startY: number; startValue: number; current: number } | null>(null);
  const inputRef = useRef<HTMLInputElement>(null);
  const id = useId();
  const shown = local ?? value;

  useEffect(() => { if (editing) inputRef.current?.select(); }, [editing]);

  useEffect(() => {
    if (!disabled || !drag.current) return;
    onGesture("end", drag.current.current);
    drag.current = null;
    setLocal(null);
  }, [disabled, onGesture]);

  const commit = (next: number) => {
    const v = clamp(next);
    onGesture("begin", shown);
    onGesture("change", v);
    onGesture("end", v);
  };

  const onPointerDown = (e: PointerEvent<HTMLDivElement>) => {
    if (disabled || e.button !== 0) return;
    e.currentTarget.setPointerCapture(e.pointerId);
    drag.current = { startY: e.clientY, startValue: value, current: value };
    setLocal(value);
    onGesture("begin", value);
  };
  const onPointerMove = (e: PointerEvent<HTMLDivElement>) => {
    if (disabled || !drag.current) return;
    const scale = e.shiftKey ? 0.1 : 1;
    const next = clamp(drag.current.startValue + ((drag.current.startY - e.clientY) / 200) * scale);
    if (next === drag.current.current) return;
    drag.current.current = next;
    setLocal(next);
    onGesture("change", next);
  };
  const onPointerUp = () => {
    if (!drag.current) return;
    onGesture("end", drag.current.current);
    drag.current = null;
    setLocal(null);
  };

  const onKeyDown = (e: KeyboardEvent) => {
    if (disabled) return;
    const fine = normalizedStep ?? (e.shiftKey ? 0.001 : 0.01);
    let next: number | null = null;
    if (e.key === "ArrowUp" || e.key === "ArrowRight") next = value + fine;
    else if (e.key === "ArrowDown" || e.key === "ArrowLeft") next = value - fine;
    else if (e.key === "PageUp") next = value + 0.1;
    else if (e.key === "PageDown") next = value - 0.1;
    else if (e.key === "Home") next = 0;
    else if (e.key === "End") next = 1;
    else if (e.key === "Delete" || e.key === "Backspace") next = defaultValue;
    else if (e.key === "Enter") { e.preventDefault(); setDraft(format(value)); setError(null); setEditing(true); return; }
    if (next === null) return;
    e.preventDefault();
    commit(next);
  };

  const submitDraft = () => {
    const parsed = parse(draft);
    if (parsed === null) { setError("Enter a number in range"); return; }
    setEditing(false);
    setError(null);
    commit(parsed);
  };

  const angle = START + shown * SWEEP;
  const r = 20;
  const arc = (from: number, to: number) => {
    const p = (a: number) => [26 + r * Math.sin((a * Math.PI) / 180), 26 - r * Math.cos((a * Math.PI) / 180)];
    const [x0, y0] = p(from);
    const [x1, y1] = p(to);
    return `M${x0} ${y0} A${r} ${r} 0 ${to - from > 180 ? 1 : 0} 1 ${x1} ${y1}`;
  };

  return (
    <div className={`knob${disabled ? " is-disabled" : ""}${overridden ? " is-overridden" : ""}`}>
      <div
        role="slider"
        tabIndex={disabled ? -1 : 0}
        aria-label={label}
        aria-valuemin={0}
        aria-valuemax={100}
        aria-valuenow={Math.round(shown * 100)}
        aria-valuetext={format(shown)}
        aria-disabled={disabled || undefined}
        aria-describedby={`${id}-hint`}
        className="knob__dial"
        style={{ width: size, height: size }}
        onPointerDown={onPointerDown}
        onPointerMove={onPointerMove}
        onPointerUp={onPointerUp}
        onPointerCancel={onPointerUp}
        onDoubleClick={() => !disabled && commit(defaultValue)}
        onKeyDown={onKeyDown}
      >
        <svg viewBox="0 0 52 52" width={size} height={size} aria-hidden="true">
          <path d={arc(START, START + SWEEP)} className="knob__track" />
          {shown > 0.001 ? <path d={arc(START, angle)} className="knob__fill" /> : null}
          <line x1="26" y1="26" x2={26 + 14 * Math.sin((angle * Math.PI) / 180)} y2={26 - 14 * Math.cos((angle * Math.PI) / 180)} className="knob__pointer" />
        </svg>
      </div>
      <span className="knob__label">{label}{overridden ? <span className="knob__override" title="Overrides inherited value"> •</span> : null}</span>
      {editing ? (
        <span className="knob__entry">
          <input
            ref={inputRef}
            aria-label={`${label} value`}
            aria-invalid={error ? true : undefined}
            value={draft}
            onChange={(e) => setDraft(e.target.value)}
            onKeyDown={(e) => { if (e.key === "Enter") submitDraft(); if (e.key === "Escape") { setEditing(false); setError(null); } }}
            onBlur={() => { setEditing(false); setError(null); }}
          />
          {error ? <span className="knob__error" role="alert">{error}</span> : null}
        </span>
      ) : (
        <button type="button" className="knob__value" disabled={disabled} onClick={() => { setDraft(format(value)); setEditing(true); }} aria-label={`Edit ${label}, ${format(shown)}`}>
          {format(shown)}{pending ? <span className="sr-only"> (sending)</span> : null}
        </button>
      )}
      <span id={`${id}-hint`} className="sr-only">Arrow keys adjust, Shift for fine, Enter to type a value, Delete to reset.</span>
    </div>
  );
}
