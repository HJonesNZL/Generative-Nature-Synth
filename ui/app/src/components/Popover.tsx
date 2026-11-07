import { useEffect, useLayoutEffect, useId, useRef, useState, type ReactNode } from "react";

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
  const panel = useRef<HTMLDivElement>(null);
  useLayoutEffect(() => {
    const anchor = button.current, menu = panel.current;
    if (!open || !anchor || !menu) return;
    const place = () => {
      const rect = anchor.getBoundingClientRect();
      const gap = 8;
      const below = window.innerHeight - rect.bottom - 2 * gap;
      const above = rect.top - 2 * gap;
      const flip = below < menu.scrollHeight && above > below;
      menu.style.maxHeight = Math.max(0, flip ? above : below) + "px";
      const width = menu.offsetWidth;
      const left = align === "end" ? rect.right - width : rect.left;
      menu.style.left = Math.max(gap, Math.min(left, window.innerWidth - width - gap)) + "px";
      menu.style.top = (flip ? rect.top - gap - menu.offsetHeight : rect.bottom + gap) + "px";
    };
    place();
    const observer = new ResizeObserver(place);
    observer.observe(menu);
    observer.observe(anchor);
    window.addEventListener("resize", place);
    document.addEventListener("scroll", place, true);
    return () => {
      observer.disconnect();
      window.removeEventListener("resize", place);
      document.removeEventListener("scroll", place, true);
    };
  }, [open, align]);
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
      {open ? <div id={id} ref={panel} className="popover__panel" role="dialog" aria-label={label}>{children(close)}</div> : null}
    </div>
  );
}
