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
   const toggle = () => document.querySelector('[role="switch"][aria-label^="Live listening ("]');
   await waitFor(()=>toggle() && !toggle().disabled,'Live listening switch');
   if ((await live()).enabled || toggle().getAttribute('aria-checked') !== 'false')
     throw Error('Live listening was already enabled.');
   toggle().click();
   const armed = await until(live,s=>s.enabled,'explicit start');
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
   toggle().click();
   const off=await until(live,s=>!s.enabled,'explicit mute');
   if ((await call('getAudioState')).status!=='disabled') throw Error('Output remained enabled.');
   result.sonification={device:device.name,listening,off,callbackFrames:audio.callbackFrames,mapObserved:true,duplicatesQuiet:true};
 }
)JS";
} // namespace atlas::standalone
