# Animal Atlas live listening concepts

These four generated studies explore how to connect an audible species voice to its detection on the current Atlas screen. Species, locations, voice shapes and counters in the pictures are illustrative. Production location labels should identify the receiver for acoustic detections and retain the source’s uncertainty. The inherited overload counter in the renders is incidental.

| Concept | Interaction |
| --- | --- |
| [Map pulses](map-pulses.png) | Show a small outlined point on detection. Pulse at confirmed note onset and label the species, synth voice and place. Let the marker fade once the note finishes. |
| [Globe voices](globe-voices.png) | Give simultaneous notes matching numbers and shapes on the globe and voice strips. The strips identify each species, location and timbre while several voices overlap. |
| [Time ribbon](time-ribbon.png) | Keep a short playback trail under the map. Align the sounding note with Now so listeners can see the delay between arrival and playback. A short playback view could be a separate tab alongside the scrollable session ranking. |
| [Ranked map](ranked-map.png) | Highlight the currently sounding species in the independently scrollable session ranking. A compact card names the receiver and synth voice and shows the envelope. |

Selected and implemented: **Time ribbon**. The 0.11.0 standalone retains thirty seconds of confirmed notes and fades each map highlight over two seconds after the last voice at that location ends. The session ranking remains a separate tab. [Working interface and release checks](../../evidence/time-ribbon/qualification.md)
