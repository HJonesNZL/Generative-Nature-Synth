import { useEffect, useId, useRef, useState, type ReactNode } from "react";

/** Disclosure popover: button toggles a panel; Escape or outside click closes and returns focus. */
export function Popover({ trigger, label, children, align = "end", className }: {
  trigger: ReactNode;
  label: string;
  children: (close: () => void) => ReactNode;
  align?: "start" | "end";
  className?: string;
}) {
  const [open, setOpen] = useState(false);
  const id = useId();
  const root = useRef<HTMLDivElement>(null);
  const button = useRef<HTMLButtonElement>(null);
  useEffect(() => {
    if (!open) return;
    const onDown = (e: MouseEvent) => { if (!root.current?.contains(e.target as Node)) setOpen(false); };
    const onKey = (e: KeyboardEvent) => { if (e.key === "Escape") { setOpen(false); button.current?.focus(); } };
    document.addEventListener("mousedown", onDown);
    document.addEventListener("keydown", onKey);
    return () => { document.removeEventListener("mousedown", onDown); document.removeEventListener("keydown", onKey); };
  }, [open]);
  const close = () => { setOpen(false); button.current?.focus(); };
  return (
    <div className={`popover${className ? ` ${className}` : ""}`} ref={root}>
      <button ref={button} type="button" className="popover__trigger" aria-expanded={open} aria-controls={id} aria-label={label} onClick={() => setOpen((o) => !o)}>
        {trigger}
      </button>
      {open ? <div id={id} className={`popover__panel popover__panel--${align}`} role="dialog" aria-label={label}>{children(close)}</div> : null}
    </div>
  );
}
