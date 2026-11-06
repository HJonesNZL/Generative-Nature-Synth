"""Deterministic data-time replay. No audio or network operations in this module."""
from bisect import bisect_left
from collections import deque
from datetime import datetime, timezone
from fractions import Fraction
from .model import parse_time, digest
from .sessions import MAX_EVENTS, content_hash

EPOCH = datetime(1970, 1, 1, tzinfo=timezone.utc)


def micros(timestamp):
    delta = parse_time(timestamp) - EPOCH
    return (delta.days * 86400 + delta.seconds) * 1000000 + delta.microseconds


class Timeline:
    def __init__(self, sessions, sid, mode="observed", speed="1"):
        if mode not in ("observed", "arrival"):
            raise ValueError("mode must be observed or arrival")
        try:
            ratio = Fraction(str(speed))
        except (ValueError, ZeroDivisionError):
            raise ValueError("invalid replay speed") from None
        if not Fraction(1, 100) <= ratio <= 10000:
            raise ValueError("speed must be 0.01-10000")
        self.session = sessions.get(sid)
        if self.session["state"] not in ("frozen", "interrupted"):
            raise ValueError("stop recording before replay")
        rows = sessions.rows(sid, limit=MAX_EVENTS)
        if content_hash([{k: v for k, v in row.items() if k != "ordinal"} for row in rows]) != self.session["content_hash"]:
            raise ValueError("session integrity failure")
        key = "observed_at_utc" if mode == "observed" else "first_seen_at_utc"
        ordered = sorted(rows, key=lambda row: (micros(row["event"][key]), row["original_seq"], row["event"]["event_id"]))
        bounds = self.session["options"].get("timeline_bounds", {}).get(mode)
        start = micros(bounds[0]) if bounds else micros(ordered[0]["event"][key]) if ordered else 0
        end = micros(bounds[1]) if bounds else micros(ordered[-1]["event"][key]) if ordered else start
        self.items = []
        for index, row in enumerate(ordered):
            offset = max(0, micros(row["event"][key]) - start)
            due = offset * ratio.denominator // ratio.numerator
            self.items.append(dict(index=index, due_us=due, original_seq=row["original_seq"], event=row["event"]))
        self.times = [item["due_us"] for item in self.items]
        self.duration_us = max((max(0, end - start) * ratio.denominator // ratio.numerator),
                               self.times[-1] + 1 if self.times else 0)
        self.mode, self.speed = mode, str(ratio)
        self.fingerprint = digest(dict(content_hash=self.session["content_hash"], mode=mode,
            speed=str(ratio), seed=self.session["options"]["seed"], bounds=bounds, ordering_version=1))

    def summary(self):
        recent, peak = deque(), 0
        for when in self.times:
            while recent and recent[0] <= when - 1000000:
                recent.popleft()
            recent.append(when)
            peak = max(peak, len(recent))
        return dict(mode=self.mode, speed=self.speed, event_count=len(self.items), duration_us=self.duration_us,
                    fingerprint=self.fingerprint, peak_events_in_one_second=peak,
                    duration_policy="scale event spacing only; no audio pitch/duration processing",
                    ordering_policy="timeline timestamp, original revision sequence, event ID",
                    seek_policy="new generation; caller clears prior scheduled actions",
                    loop_policy="caller resets generation and voice/effect state")

    def page(self, after_index=-1, limit=200):
        if type(after_index) is not int or after_index < -1 or type(limit) is not int or not 1 <= limit <= 500:
            raise ValueError("invalid replay page")
        items = self.items[after_index + 1:after_index + 1 + limit]
        return dict(**self.summary(), events=items, next_index=items[-1]["index"] if items else after_index,
                    has_more=after_index + 1 + len(items) < len(self.items))

    def block(self, start_frame, frames, sample_rate, capacity=256):
        """Offline worker/oracle only. Native callback bridge must preallocate independently.

        Half-open block boundaries; rounding down to a sample is versioned policy.
        Over-capacity blocks retain the earliest actions and explicitly report omissions.
        """
        if any(type(v) is not int for v in (start_frame, frames, sample_rate, capacity)) or start_frame < 0 or not 1 <= frames <= 65536 or not 8000 <= sample_rate <= 384000 or not 1 <= capacity <= 4096:
            raise ValueError("invalid render block")
        # ceil(frame * 1e6 / rate) is the earliest integer microsecond that maps into frame.
        left_us = (start_frame * 1000000 + sample_rate - 1) // sample_rate
        right_us = ((start_frame + frames) * 1000000 + sample_rate - 1) // sample_rate
        left, right = bisect_left(self.times, left_us), bisect_left(self.times, right_us)
        items = [dict(item, sample_offset=item["due_us"] * sample_rate // 1000000 - start_frame)
                 for item in self.items[left:min(right, left + capacity)]]
        return dict(events=items, omitted=max(0, right - left - capacity), total_in_block=right - left)
