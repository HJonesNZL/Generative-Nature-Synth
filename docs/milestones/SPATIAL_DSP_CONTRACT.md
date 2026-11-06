# Regional mixing and ambience contract

7 November 2026, next authorised milestone. This contract precedes implementation. Artistic geographic regions use reported positions; they do not infer habitats, movement, call recordings or real acoustic spaces. No external dependency or network request is needed.

## Behaviour and ownership

`AudioSettings.spatial` contains `SpatialSettings { enabled=false, amount=.25, regions }`. At most eight ordered `RegionSettings` carry `id`, `name`, `south=-90`, `north=90`, `west=-180`, `east=180`, `pan=0`, `gainDb=0`, `send=.25`, `echoMix=.3`, `delayMs=180`, `feedback=.25`, `roomSeconds=1.2`, `tone=.5`, and `positionPan=false`.

IDs are unique ASCII letters/digits/underscore/hyphen, 1–64 bytes. Names are valid UTF-8, 1–128 bytes, without controls. Latitude bounds are within ±90 and south<north; longitude within ±180. Equal longitudes and [180,-180] are invalid zero-width regions. [-180,180] is full world; west>east otherwise crosses the dateline. Endpoints are inclusive, ±180 identify the same meridian, and the first matching region wins. Missing/outside positions use unassigned route 0. Station observations route by reported station position, not fictitious animal tracks. Region slots 1–8 and original event references are compiled into actions on the worker, then captured per voice at onset/retirement.

Pan [-1,1], gain [-60,0] dB, send/echoMix/tone/amount [0,1], delay [20,500] ms, feedback [0,.5], nominal room decay [.2,2.5] seconds. Inputs must be finite. All-or-nothing configuration changes reprepare quietly and reset effect histories, with generation fencing; no continuous delay modulation or parameter automation is claimed. Construction/allocation/destruction belong to the control worker. The callback only consumes fixed numeric route/coefficient/state arrays.

## Signal and state equations

Spatial disabled preserves the old exact dual-mono path. Enabled dry pan uses theta=(pan+1)*pi/4 and gains cos(theta), sin(theta); centre is intentionally -3dB per channel relative to dual mono. Position pan replaces fixed dry pan with longitude mapped across that region, including dateline regions. Wet returns use fixed region pan. Unassigned dry uses centred equal-power pan. Region gain applies to both dry and send. No master gain increase or hidden limiter is introduced; mono audition uses (L+R)/2.

Each region has two echo delays: D and D+min(17ms,.1D), plus four independent damped comb delays of 41/59/89/127ms. These are original design choices, not copied historic reverberator constants. Delay lengths round to sample frames during preparation. For each sample read delayed state first, update feedback, then write and advance. Echo write z[n]=(1-f)*x[n]+f*z[n-D]. Its wet return is z[n-D], with no present dry component. Comb feedback uses lp[n]=(1-a)*z[n-D]+a*lp[n-1], then z[n]=(1-g)*x[n]+g*lp[n]. The wet return is the delayed read. g=10^(-3*D/(sampleRate*roomSeconds)); a=exp(-2*pi*cutoff/sampleRate); tone maps 200Hz to min(12kHz,.45*sampleRate). The low-pass adds loop delay, so roomSeconds is nominal feedback decay, not a measured room T60.

Room left weights are .4/.3/.2/.1, right are reversed. Weights are positive and sum to one. EchoMix blends echo and room returns convexly. Send scales region input. All feedback updates are convex combinations, bounding each state/region return by the largest prior input; |feedback|<1. Subnormal-state handling and nonfinite recovery must be explicit and tested if implemented.

Different regions can return historical energy simultaneously. Therefore average the region wet returns by the **configured** region count, not the active voice count. An active wet plan has amount>0 and at least one compiled action routed to a region with send>0. Its complete output is `(dry + amount*averageWet)/(1+amount)`. This fixed compensation also applies to unassigned dry and silent-send regions in that piece; adding a region can change wet level. With the existing .8 dry bound and per-channel pan gains <=1, the result remains <=.8 before nonpositive master gain. When no action can feed an audible wet path, bypass compensation and effect storage/tails entirely; enabled dry panning remains.

## Time, resources and failure

No lookahead or dry-path latency. Effects use bounded preallocated storage: maximum eight buses, 48 delay lines, about 16.4MB with double samples at 192kHz. No allocation, deallocation, locks, I/O, logging, strings, geographic tests or exponentials belong in callback processing. Prepare at the actual sample rate; rate/config changes construct new worker-owned state, published and retired through existing playback ownership.

Pause freezes score and DSP after its normal output ramp. Stop/seek/panic use O(1) per-line logical resets: index and valid-sample counts prevent stale reads until overwritten, and filter states reset. They must not clear large buffers in a callback. Natural end reserves 12 seconds after the last possible voice end when wet is active, with the last 20ms fading to exact zero; this is an explicit finite horizon, not infinite physical decay. Existing 30-minute total capacity includes musical and effect tails. Oversized/invalid plans fail before replacing current state. Nonfinite state/output must recover observably to silence without callback exceptions.

## Verification and evidence boundary

Independent impulse timing/recurrence checks, stereo routing including dateline/overlap/missing positions, finite/bounded sums across alternating regions, mono consequences, long-tail/reset/seek/pause checks, partition invariance, no callback allocations, low/high sample rates and worst bounded region memory. Save/recall/render parity must include complete ordered regions, composition and voices, and retain schema1/2 content hashes and PCM. UI checks cover full draft validation, stale requests, save barriers, quiet recall and immutable WAVs. Release benchmarks report their workload and cannot certify physical audio/DAW deadlines or subjective quality.

Generic parallel feedback-comb concepts follow [Julius O. Smith's explanation](https://www.dsprelated.com/freebooks/pasp/Schroeder_Reverberators.html); no external implementation is copied. Equal-power pan follows the directly derived identity cos²(theta)+sin²(theta)=1. Original tunings and listening judgements remain distinct from the mathematical stability argument. This initial echo/comb ambience is not a claim of a diffuse commercial reverb or listener-approved regional sound.
