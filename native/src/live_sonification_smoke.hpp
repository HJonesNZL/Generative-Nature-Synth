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
   const selectedMode = 'SONIFICATION_MODE';
   const smoothed = selectedMode === 'smoothed';
   const until = async (read, accepts, description, timeoutMs=18000) => {
     const end = Date.now()+timeoutMs;
     do {
       const value = await read();
       if (accepts(value)) return value;
       if (value.status === 'error') throw Error(value.error);
       await new Promise(resolve => setTimeout(resolve,150));
     } while (Date.now()<end);
     throw Error('Timed out waiting for '+description);
   };
   // Starting collection may reopen the managed library and reload WebKit.
   if (!(await call('getLiveSources')).collecting) {
     const collect = document.querySelector('[aria-label="Start live collection"]');
     if (!collect) throw Error('Collect button missing at session start.');
     collect.click();
     const choices = () => [...document.querySelectorAll('.live-sources__choices label')];
     const start = () => [...document.querySelectorAll('.live-sources__menu button')].find(b=>b.textContent==='Start live updates');
     await waitFor(()=>start() && !start().disabled && choices().length>=3, 'collection controls');
     for (const label of choices()) {
       const want = label.textContent.trim()==='NZ bird detections';
       const input = label.querySelector('input');
       if (input.checked!==want) {
         input.click();
         await new Promise(resolve=>setTimeout(resolve,30));
       }
     }
     start().click();
   }
   const collection=await until(()=>call('getLiveSources'),s=>!!s.session && s.collecting,'Collect receipt');
   const scoped=()=>call('getLiveChanges',{sessionId:collection.session.id,storeId:collection.session.storeId,afterSequence:collection.session.baselineRevision,limit:16});
   if ((await scoped()).events.length) throw Error('Old library populated a new session.');
   const statusButton = document.querySelector('[aria-label="Live listening status"]');
   if (!statusButton) throw Error('Live listening status control missing.');
   statusButton.click();
   const mode = () => document.querySelector('select[aria-label="Live mode"]');
   const density = () => document.querySelector('select[aria-label="Live density"]');
   await waitFor(()=>mode() && !mode().disabled && !mode().closest('fieldset')?.disabled,
     'live mode control');
   const dialog=mode().closest('[role="dialog"]').getBoundingClientRect();
   if (dialog.left<0 || dialog.right>window.innerWidth || dialog.top<0 || dialog.bottom>window.innerHeight)
     throw Error('Listening menu extends outside the window.');
   if (![...mode().options].some(option=>option.value===selectedMode))
     throw Error('Requested live mode is unavailable in React.');
   mode().value=selectedMode; mode().dispatchEvent(new Event('change',{bubbles:true}));
   await until(live,s=>s.mode===selectedMode,'React live mode selection');
   if (!smoothed) {
     await waitFor(()=>density() && !density().disabled && !density().closest('fieldset')?.disabled,
       'musical density control');
     density().value='4'; density().dispatchEvent(new Event('change',{bubbles:true}));
     await until(live,s=>s.mode==='musical' && s.density===4,'React musical density selection');
   } else if (density()) throw Error('Smoothed mode exposed musical density.');
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
   const retiredPolls=[];
   window.atlasHost.request = async function(request, ...args) {
     const track = observing && !request.requestId.startsWith('shell-smoke-') && Object.hasOwn(polls,request.method);
     const reply = await originalRequest.call(this,request,...args);
     if (track) {
       // Changing the visible outcome retires its old React query by design.
       if (args[0]?.aborted && reply.error?.code==='aborted') {
         retiredPolls.push({method:request.method,code:reply.error.code,message:reply.error.message});
         return reply;
       }
       ++polls[request.method];
       if (request.method==='getAudioState' && reply.ok) {
         if (reply.result.meterIsSimulated===false && Number.isFinite(reply.result.peakDbfs))
           outputPeakDbfs=Math.max(outputPeakDbfs,reply.result.peakDbfs);
       }
       if (request.method==='getLiveOutcome' && reply.ok && reply.result?.outcome)
         renderedOutcomes.set(reply.result.outcome.eventId,reply.result.outcome);
       if (!reply.ok || !reply.result || (request.method === 'getAudioState' && reply.result.status !== 'running'))
         pollError = request.method+' failed during live playback: '+JSON.stringify(reply);
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
     page=>page.events.some(r=>r.change==='corrected') && page.events.some(r=>r.event.id.endsWith('fixture-backfill')),
     'committed correction and backfill');
   await new Promise(resolve=>setTimeout(resolve,500));
   if ((await scoped()).events.some(r=>r.event.id.endsWith('fixture-backfill'))) throw Error('Delayed pre-session observation entered the map feed.');
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
   const densePolls = {...polls};
   let pendingObserved=null;
   const dense=await until(live,s=>{
     if (smoothed && s.pending>0 && s.admitted<501 && !pendingObserved)
       pendingObserved={received:s.received,admitted:s.admitted,pending:s.pending,catchingUp:s.catchingUp};
     return s.received===501 && (smoothed ? s.admitted===501 && s.pending===0 && !s.catchingUp :
       s.admitted+s.grouped+s.dropped===s.received);
   },'settled dense note accounting',smoothed ? 65000 : 18000);
   if (smoothed) {
     if (dense.mode!=='smoothed' || !pendingObserved || dense.grouped!==0 ||
         dense.dropped!==0 || dense.skippedRevisions!==0)
       throw Error('Dense smoothed notes did not drain exactly once without grouping or loss.');
   } else if (dense.mode!=='musical' || dense.density!==4 || dense.admitted<2 ||
       dense.admitted>5 || dense.grouped<=0 || dense.dropped<=0)
     throw Error('Dense phrase did not stay within its musical grouping budget.');
   await new Promise(resolve=>setTimeout(resolve,1800));
   const ranking=await call('getLiveSpeciesCounts',{sessionId:collection.session.id,limit:16});
   const counts=ranking.species.map(row=>row.count);
   if (!ranking.sessionActive || counts.length!==8 || counts.reduce((a,b)=>a+b,0)!==501 ||
       counts.some((value,i)=>i>0 && value>counts[i-1]) ||
       ranking.species.some(row=>row.speciesId.endsWith(':818181')))
     throw Error('Historical or incomplete species ranking.');
   await waitFor(()=>[...document.querySelectorAll('.grain__scroll .grain__count')]
     .map(span=>Number(span.textContent)).join(',')===counts.join(','),'live species ranking on screen');
   window.scrollTo(0,document.documentElement.scrollHeight);
   if (window.scrollY!==0) throw Error('Atlas scrolled beyond its workspace.');
   const settled=await live();
   if (settled.admitted!==dense.admitted || settled.grouped!==dense.grouped ||
       settled.dropped!==dense.dropped || settled.received!==501)
     throw Error('Dense phrase accounting changed after its bounded completion.');
   const denseAudio=await call('getAudioState');
   if (denseAudio.status!=='running' || BigInt(denseAudio.callbackFrames)<=BigInt(audio.callbackFrames))
     throw Error('Output did not continue through dense phrase playback.');
   if (smoothed && denseAudio.liveTriggeredNotes!=='501')
     throw Error('Audio callback did not confirm exactly 501 triggered notes.');
   const densePage=await call('getChanges',{storeId:before.storeId,afterSequence:arrived.nextSequence,limit:8});
   const denseEvent=densePage.events.find(row=>row.event.id.includes('fixture-dense-'))?.event;
   if (!denseEvent) throw Error('Dense observations are missing from changes.');
   if (polls.getAudioState<=densePolls.getAudioState || polls.getLiveOutcome<=densePolls.getLiveOutcome)
     throw Error('React audio or selected-outcome polling stopped during dense playback.');
   // Selecting the sparse event opened Selected and unmounted the latest list.
   // Use the real tab before locating a fresh dense observation on the map.
   const latestTab=document.querySelector('[role="tab"][aria-controls="panel-incoming"]');
   if (!latestTab) throw Error('Latest detections tab is missing.');
   latestTab.click();
   const denseRow=()=>[...document.querySelectorAll('#panel-incoming .latest-detections__row')]
     .find(button=>button.getAttribute('aria-label')==='Show Fixture live bird on map' &&
       button.querySelector('time')?.dateTime===denseEvent.observedAtUtc);
   await waitFor(()=>!!denseRow(),'dense observation in Latest detections');
   denseRow().click();
   await waitFor(()=>selectedDisclosure() && [...renderedOutcomes.values()].some(outcome=>
     outcome.eventId.includes('fixture-dense-') && (smoothed ? outcome.status==='sounded' : true)),
     'React selected dense observation outcome');
   checkHeader();
   if (pollError) throw Error('Live playback header: '+pollError);
   statusButton.click();
   await waitFor(()=>[...document.querySelectorAll('.menu__note')].some(note=>
     note.textContent.includes('501 received') && (smoothed ? note.textContent.includes('501 sounded') && note.textContent.includes('0 pending') : note.textContent.includes(settled.grouped+' grouped'))),
     'React dense received and grouped counters');
   if (mode()?.value!==selectedMode || (smoothed ? density()!==null : density()?.value!=='4'))
     throw Error('React lost live settings during dense playback.');
   statusButton.click();
   observing = false;
   observer.disconnect();
   window.atlasHost.request = originalRequest;
   toggle().click();
   const off=await until(live,s=>!s.enabled,'explicit mute');
   const offAudio=await call('getAudioState');
   if (offAudio.status!=='disabled' || offAudio.peakDbfs!==null) throw Error('Output remained enabled or meter did not clear.');
   result.sonification={sessionFiltered:true,rankedCounts:counts,retiredPolls,mode:selectedMode,pendingObserved,denseMapObserved:true,liveTriggeredNotes:denseAudio.liveTriggeredNotes,device:device.name,listening,off,callbackFrames:audio.callbackFrames,mapObserved:true,duplicatesQuiet:true,polls,headerErrorFree:true,selectedOutcome,dense:settled,
     outputPeakDbfs,denseCallbackFrames:denseAudio.callbackFrames,denseCountersRendered:true,muteMeterCleared:true};
 }
)JS";
} // namespace atlas::standalone
