import type { HostMethods } from "../../host/contracts";

export const SESSION_PAGE_SIZE = 50;
export const SESSION_BACK_HISTORY_LIMIT = 128;

export type SessionBrowserPage = HostMethods["listSessionsPage"]["response"];

/** One page plus bounded cursor history; never retains the whole library. */
export interface SessionBrowserSnapshot {
  page: SessionBrowserPage;
  cursor: string | null;
  previousCursors: (string | null)[];
  pageNumber: number;
}

export interface SessionPageTarget {
  cursor: string | null;
  previousCursors: (string | null)[];
  pageNumber: number;
}

export function firstSessionPage(): SessionPageTarget {
  return { cursor: null, previousCursors: [], pageNumber: 1 };
}

export function nextSessionPage(snapshot: SessionBrowserSnapshot): SessionPageTarget | null {
  if (snapshot.page.nextCursor === null) return null;
  return {
    cursor: snapshot.page.nextCursor,
    previousCursors: [...snapshot.previousCursors, snapshot.cursor].slice(-SESSION_BACK_HISTORY_LIMIT),
    pageNumber: snapshot.pageNumber + 1,
  };
}

export function previousSessionPage(snapshot: SessionBrowserSnapshot): SessionPageTarget | null {
  if (snapshot.previousCursors.length === 0) return null;
  return {
    cursor: snapshot.previousCursors[snapshot.previousCursors.length - 1]!,
    previousCursors: snapshot.previousCursors.slice(0, -1),
    pageNumber: snapshot.pageNumber - 1,
  };
}

export function acceptSessionPage(page: SessionBrowserPage, target: SessionPageTarget, previous: SessionBrowserSnapshot | null): SessionBrowserSnapshot | null {
  if (target.cursor !== null && previous && (page.sourceId !== previous.page.sourceId || page.revision !== previous.page.revision)) return null;
  return { page, ...target };
}
