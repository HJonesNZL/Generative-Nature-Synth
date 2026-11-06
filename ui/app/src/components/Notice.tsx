import type { ReactNode } from "react";
import { Icon } from "./Icon";

/** Status message with an icon and a text prefix, so state is never colour-only. */
export function Notice({ tone, title, children, action, live = false }: {
  tone: "info" | "caution" | "problem";
  title: string;
  children?: ReactNode;
  action?: ReactNode;
  live?: boolean;
}) {
  return (
    <div className={`notice notice--${tone}`} role={tone === "problem" ? "alert" : live ? "status" : undefined}>
      <Icon name={tone === "info" ? "info" : "warning"} size={14} />
      <div className="notice__body">
        <strong className="notice__title">{title}</strong>
        {children ? <span className="notice__text">{children}</span> : null}
      </div>
      {action ? <div className="notice__action">{action}</div> : null}
    </div>
  );
}

export function EmptyState({ title, children }: { title: string; children?: ReactNode }) {
  return (
    <div className="empty-state">
      <strong>{title}</strong>
      {children ? <span>{children}</span> : null}
    </div>
  );
}
