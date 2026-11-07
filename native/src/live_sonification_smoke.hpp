#pragma once
namespace atlas::standalone {
// Invoked only with an explicit disposable state, smoke receipt and virtual
// device. The companion collector supplies observations after each milestone.
inline constexpr auto liveSonificationSmoke = R"JS(
 {
   const device = result.audioDevices.devices.find(row => row.name === 'ZoomAudioDevice');
   if (!device || device.isDefault) throw Error('A separate ZoomAudioDevice virtual output is required.');
   const cat = result.audioDevices;
   const configured = await call('setAudioDevice', {controlRevision:cat.controlRevision,
     preparationId:cat.preparationId, generation:cat.generation,
     configuration:{mode:'explicitDevice',deviceId:device.id,bufferFrames:0}});
   if (configured.configuration.deviceId !== device.id || configured.active)
     throw Error('Could not select the isolated virtual output.');
   const before = await call('getChanges',{storeId:'', afterSequence:'0', limit:16});
   const live = () => call('getLiveState');
   const until = async (read, accepts, description) => {
     const end = Date.now()+18000;
     do {
       const value = await read();
       if (accepts(value)) return value;
       if (value.status === 'error') throw Error(value.error);
       await new Promise(resolve => setTimeout(resolve,150));
     } while (Date.now()<end);
     throw Error('Timed out waiting for '+description);
   };
   const statusButton = document.querySelector('[aria-label="Live listening status"]');
   if (!statusButton) throw Error('Live listening status control missing.');
   statusButton.click();
   const mode = () => document.querySelector('select[aria-label="Live mode"]');
   const density = () => document.querySelector('select[aria-label="Live density"]');
   await waitFor(()=>mode() && !mode().disabled && !mode().closest('fieldset')?.disabled,
     'musical mode control');
   mode().value='musical'; mode().dispatchEvent(new Event('change',{bubbles:true}));
   await until(live,s=>s.mode==='musical','React musical mode selection');
   await waitFor(()=>density() && !density().disabled && !density().closest('fieldset')?.disabled,
     'musical density control');
   density().value='4'; density().dispatchEvent(new Event('change',{bubbles:true}));
   await until(live,s=>s.mode==='musical' && s.density===4,'React musical density selection');
   statusButton.click();
   const toggle = () => document.querySelector('[role="switch"][aria-label^="Live listening ("]');
   await waitFor(()=>toggle() && !toggle().disabled,'Live listening switch');
   if ((await live()).enabled || toggle().getAttribute('aria-checked') !== 'false')
     throw Error('Live listening was already enabled.');
   // Observe the production React requests, excluding this smoke's raw bridge
   // calls. A raw successful host reply alone does not prove client validation.
   const polls = {getTransport:0,getAudioState:0,getLiveOutcome:0};
   let outputPeakDbfs=-96;
   const renderedOutcomes = new Map();
   const originalRequest = window.atlasHost.request;
   let pollError = '', observing = false;
   window.atlasHost.request = async function(request, ...args) {
     const track = observing && !request.requestId.startsWith('shell-smoke-') && Object.hasOwn(polls,request.method);
     const reply = await originalRequest.call(this,request,...args);
     if (track) {
       ++polls[request.method];
       if (request.method==='getAudioState' && reply.ok) {
         if (reply.result.meterIsSimulated===false && Number.isFinite(reply.result.peakDbfs))
           outputPeakDbfs=Math.max(outputPeakDbfs,reply.result.peakDbfs);
       }
       if (request.method==='getLiveOutcome' && reply.ok && reply.result?.outcome)
         renderedOutcomes.set(reply.result.outcome.eventId,reply.result.outcome);
       if (!reply.ok || !reply.result || (request.method === 'getAudioState' && reply.result.status !== 'running'))
         pollError = request.method+' failed during live playback';
     }
     return reply;
   };
   const checkHeader = () => {
     const error = document.querySelector('.app-header .header-error')?.textContent;
     if (observing && error) pollError = error;
   };
   const observer = new MutationObserver(checkHeader);
   observer.observe(document.querySelector('.app-header'),{subtree:true,childList:true,characterData:true});
   toggle().click();
   const armed = await until(live,s=>s.enabled,'explicit start');
   observing = true;
   checkHeader();
   if (armed.admitted !== 0) throw Error('Startup backlog sounded.');
   if ((await call('getAudioDevices')).active?.deviceId !== device.id) throw Error('Unexpected output route.');
   result.feedStage='armed';
   const listening = await until(live,s=>s.admitted===1,'new detection onset');
   const arrived = await call('getChanges',{storeId:before.storeId,afterSequence:before.nextSequence,limit:16});
   const fresh = arrived.events.find(row=>row.change==='new' && row.liveEligible)?.event;
   if (!fresh || !fresh.position) throw Error('Expected geolocated new event.');
   const row = () => [...document.querySelectorAll('.latest-detections__row')].find(button=>
     button.getAttribute('aria-label')==='Show Fixture live bird on map' && button.querySelector('time')?.dateTime===fresh.observedAtUtc);
   await waitFor(()=>!!row(),'mapped new detection');
   row().click();
   const selectedDisclosure = () => [...document.querySelectorAll('.inspector details')]
     .find(details=>details.querySelector('summary')?.textContent.startsWith('Live musical outcome'));
   await waitFor(()=>selectedDisclosure()?.querySelector('summary')?.textContent.includes('sounded') &&
     renderedOutcomes.get(fresh.id)?.status==='sounded','React selected event sounded outcome');
   selectedDisclosure().open=true;
   const selectedOutcome=renderedOutcomes.get(fresh.id);
   if (selectedOutcome.speciesId!==fresh.speciesId ||
       !selectedDisclosure().textContent.includes('Fixture live bird'))
     throw Error('Selected event outcome lost species provenance.');
   result.feedStage='admitted';
   await until(()=>call('getChanges',{storeId:before.storeId,afterSequence:before.nextSequence,limit:16}),
     page=>page.events.some(r=>r.change==='corrected') && page.events.some(r=>r.change==='baseline'),
     'committed correction and backfill');
   await new Promise(resolve=>setTimeout(resolve,500));
   const quiet = await live();
   const audio = await call('getAudioState');
   if (quiet.received!==1 || quiet.admitted!==1 || quiet.dropped || audio.status!=='running' ||
       BigInt(audio.callbackFrames)<=0n) throw Error('Correction, duplicate or device accounting failed.');
   await waitFor(()=>toggle()?.getAttribute('aria-checked')==='true','listening status');
   await waitFor(()=>polls.getTransport>=3 && polls.getAudioState>=3,
     'production React transport and audio polling while listening');
   // Let React consume the observed replies and render validation failures.
   await new Promise(resolve=>setTimeout(resolve,300));
   checkHeader();
   if (pollError) throw Error('Live listening header: '+pollError);
   if (!/^-?[0-9.]+ dB$/.test(document.querySelector('.app-header .master__db')?.textContent ?? ''))
     throw Error('React did not render the running output level.');
   if (outputPeakDbfs<=-96)
     throw Error('React audio polls never observed actual PCM output.');
   result.feedStage='dense-ready';
   const dense=await until(live,s=>s.received===501 &&
     s.admitted+s.grouped+s.dropped===s.received,'settled dense phrase accounting');
   if (dense.mode!=='musical' || dense.density!==4 || dense.admitted<2 ||
       dense.admitted>5 || dense.grouped<=0 || dense.dropped<=0)
     throw Error('Dense phrase did not stay within its musical grouping budget.');
   await new Promise(resolve=>setTimeout(resolve,1800));
   const settled=await live();
   if (settled.admitted!==dense.admitted || settled.grouped!==dense.grouped ||
       settled.dropped!==dense.dropped || settled.received!==501)
     throw Error('Dense phrase accounting changed after its bounded completion.');
   const denseAudio=await call('getAudioState');
   if (denseAudio.status!=='running' || BigInt(denseAudio.callbackFrames)<=BigInt(audio.callbackFrames))
     throw Error('Output did not continue through dense phrase playback.');
   checkHeader();
   if (pollError) throw Error('Live musical header: '+pollError);
   statusButton.click();
   await waitFor(()=>[...document.querySelectorAll('.menu__note')].some(note=>
     note.textContent.includes('501 received') && note.textContent.includes(settled.grouped+' grouped')),
     'React dense received and grouped counters');
   if (mode()?.value!=='musical' || density()?.value!=='4')
     throw Error('React lost musical settings during dense playback.');
   statusButton.click();
   observing = false;
   observer.disconnect();
   window.atlasHost.request = originalRequest;
   toggle().click();
   const off=await until(live,s=>!s.enabled,'explicit mute');
   const offAudio=await call('getAudioState');
   if (offAudio.status!=='disabled' || offAudio.peakDbfs!==null) throw Error('Output remained enabled or meter did not clear.');
   result.sonification={device:device.name,listening,off,callbackFrames:audio.callbackFrames,mapObserved:true,duplicatesQuiet:true,polls,headerErrorFree:true,selectedOutcome,dense:settled,
     outputPeakDbfs,denseCallbackFrames:denseAudio.callbackFrames,denseCountersRendered:true,muteMeterCleared:true};
 }
)JS";
} // namespace atlas::standalone
