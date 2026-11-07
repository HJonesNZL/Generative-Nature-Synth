import test from 'node:test';
import assert from 'node:assert/strict';
import { acceptSessionPage, firstSessionPage, nextSessionPage, previousSessionPage, SESSION_BACK_HISTORY_LIMIT } from '../src/screens/atlas/sessionBrowserState.ts';
const page = (number, nextCursor = `cursor-${number + 1}`) => ({ items: [{ id: `piece-${number}` }], nextCursor, sourceId: 'synthetic-library', revision: '7' });

test('older saved pieces are reachable and back navigation restores the cursor', () => {
  const first = acceptSessionPage(page(1), firstSessionPage(), null);
  const second = acceptSessionPage(page(2), nextSessionPage(first), first);
  assert.equal(second.page.items[0].id, 'piece-2');
  assert.equal(second.pageNumber, 2);
  assert.deepEqual(previousSessionPage(second), firstSessionPage());
  const last = acceptSessionPage(page(3, null), nextSessionPage(second), second);
  assert.equal(nextSessionPage(last), null);
});

test('thousands of pages retain only one payload and bounded back history, without limiting forward access', () => {
  let snapshot = acceptSessionPage(page(1), firstSessionPage(), null);
  for (let number = 2; number <= 5000; ++number) snapshot = acceptSessionPage(page(number), nextSessionPage(snapshot), snapshot);
  assert.equal(snapshot.pageNumber, 5000);
  assert.equal(snapshot.page.items.length, 1);
  assert.equal(snapshot.previousCursors.length, SESSION_BACK_HISTORY_LIMIT);
  assert.equal(nextSessionPage(snapshot).cursor, 'cursor-5001');
  for (let i = 0; i < SESSION_BACK_HISTORY_LIMIT; ++i) snapshot = acceptSessionPage(page(snapshot.pageNumber - 1), previousSessionPage(snapshot), snapshot);
  assert.equal(snapshot.pageNumber, 5000 - SESSION_BACK_HISTORY_LIMIT);
  assert.equal(previousSessionPage(snapshot), null);
  assert.equal(nextSessionPage(snapshot).cursor, `cursor-${snapshot.pageNumber + 1}`);
});

test('revision/library mismatches do not replace an existing page, while an explicit first-page refresh accepts new identity', () => {
  const snapshot = acceptSessionPage(page(1), firstSessionPage(), null);
  for (const changed of [{ ...page(2), revision: '8' }, { ...page(2), sourceId: 'other-library' }]) {
    assert.equal(acceptSessionPage(changed, nextSessionPage(snapshot), snapshot), null);
    assert.equal(snapshot.page.items[0].id, 'piece-1');
    assert.equal(acceptSessionPage(changed, firstSessionPage(), snapshot).page, changed);
  }
});
