// Strict collection-session UI qualification; all data and bridge replies are disposable fixtures.
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {mkdir,writeFile} from 'node:fs/promises';
import {resolve} from 'node:path';
const {chromium}=await import(process.env.PLAYWRIGHT_MODULE || 'playwright');
const port=Number(process.env.ATLAS_SESSION_UI_PORT || 5198),origin=`http://127.0.0.1:${port}`;
const output=resolve('test-results/live-session');
const server=spawn(process.execPath,['node_modules/vite/bin/vite.js','--port',String(port),'--strictPort'],{env:{...process.env,ATLAS_LIBRARY:''},stdio:['ignore','pipe','pipe']});
server.stdout.resume();server.stderr.resume();let browser;
try {
  for(let n=0;n<100;n++){try{if((await fetch(origin)).ok)break;}catch{}await new Promise(r=>setTimeout(r,50));}
  browser=await chromium.launch({headless:true,channel:'chrome',args:['--enable-unsafe-swiftshader']});
  const page=await browser.newPage({viewport:{width:1440,height:900},reducedMotion:'reduce'});
  const errors=[];page.on('pageerror',e=>errors.push(e.message));page.setDefaultTimeout(12000);
  await page.addInitScript(()=>{
    const f=window.sessionFixture={calls:[],liveEnabled:false,session:null,generation:0,allowEvents:false,holdNext:false,held:false,oldCurrent:null,leaks:[]};
    const source=(id,enabled)=>({id,enabled,polling:false,lastAttemptUtc:null,lastSuccessUtc:null,lastObservationUtc:'2020-01-01T00:00:00Z',error:null,records:'600000',complete:false,gap:false,nextPollSeconds:enabled?60:0});
    const state=()=>({collecting:!!f.session,libraryActive:true,session:f.session,sessionError:f.sessionError??null,sources:[source('birdweather',!!f.session),source('nzsharks',false)]});
    const event=(id,name,stamp,sequence)=>({id,revisionSequence:String(sequence),sourceId:'birdweather',speciesId:id,commonName:name,individualId:null,deploymentId:null,stationId:'receiver',eventKind:'acoustic_species_detection',locationKind:'station_detection',position:{latitude:-41,longitude:174,uncertaintyMetres:null},observedAtUtc:stamp,firstSeenAtUtc:stamp,confidence:null,attribution:'Synthetic UI fixture',qualityFlags:[],provenance:'live_source'});
    const historic=event('historic','Historical seal','2020-01-01T00:00:00Z',1);
    const methods=['getCapabilities','getLiveSources','setLiveSources','getLiveChanges','getLiveSpeciesCounts','getChanges','getCatalog','getLiveOutcome','queryHistory','getLiveState','setLiveEnabled','getAudioState'];
    window.atlasHost={request:async request=>{
      const {method,args}=request;f.calls.push({method,args,at:Date.now()});let result;
      if(method==='getCapabilities')result={protocolVersion:1,implementation:'native',supportedMethods:methods,audioAvailable:true,libraryAvailable:true,maxEventBatch:500,maxVisibleMarkers:400};
      else if(method==='getLiveSources')result=state();
      else if(method==='getLiveState'||method==='setLiveEnabled'){
        if(method==='setLiveEnabled')f.liveEnabled=args.enabled;
        result={enabled:f.liveEnabled,status:f.liveEnabled?'listening':'off',received:0,admitted:0,dropped:0,skippedRevisions:0,pending:0,error:''};
      } else if(method==='getAudioState')result={status:f.liveEnabled?'running':'disabled',deviceName:null,masterGainDb:-18,peakDbfs:null,meterIsSimulated:false,clipping:false,activeSynthVoices:0,maxSynthVoices:24};
      else if(method==='setLiveSources'){
        if(!args.sourceIds.length && f.failStop){f.failStop=false;await new Promise(resolve=>setTimeout(resolve,80));return {ok:false,requestId:request.requestId,error:{code:'unavailable',message:'Synthetic stop rejected'}};}
        if(args.sourceIds.length){if(!f.session){f.generation++;f.allowEvents=false;f.filteredServed=false;f.withdraw=false;f.withdrawalSent=false;f.session={id:'collection-'+f.generation,storeId:'session-library',baselineRevision:String(f.generation*100),observedFromUtc:new Date().toISOString()};}}
        else f.session=null;
        result=state();result.sources=result.sources.map(row=>({...row,enabled:args.sourceIds.includes(row.id)}));
      } else if(method==='getCatalog')result={species:[{id:'historic',commonName:'Historical seal',scientificName:'Historical taxon',group:'unclassified'}],stations:[{id:'receiver',name:'Current receiver',sourceId:'birdweather',latitude:-41,longitude:174,positionDescription:'Receiver'}],sources:[{id:'birdweather',name:'Fixture source',measurement:'acoustic detection',attribution:'Fixture'}],limited:true};
      else if(method==='getChanges')result={storeId:'session-library',nextSequence:'1',baselineTotal:600000,events:[{event:historic,change:'baseline',liveEligible:false}]};
      else if(method==='getLiveChanges'){
        const s=f.session;
        if(!s || args.sessionId!==s.id)result={storeId:'session-library',nextSequence:'0',sessionId:s?.id??'',sessionActive:false,events:[]};
        else {
          const seq=f.generation*100+2,stamp=new Date(Date.parse(s.observedFromUtc)+1).toISOString();
          const current=event('current-'+f.generation,'Current bird '+f.generation,stamp,seq);
          const delayed=event('delayed','Delayed pre-Collect bird',new Date(Date.parse(s.observedFromUtc)-3600000).toISOString(),seq-1);
          const events=f.allowEvents && BigInt(args.afterSequence)<BigInt(seq)?[historic,delayed,...(f.oldCurrent?[f.oldCurrent]:[]),current].map(event=>({event,change:'new',liveEligible:true})):[];
          result={storeId:s.storeId,nextSequence:f.allowEvents?String(seq):s.baselineRevision,sessionId:s.id,sessionActive:true,events};
          if(f.allowEvents && !f.filteredServed){f.filteredServed=true;result.events=[];result.nextSequence=String(seq-1);result.hasMore=true;}
          if(f.withdraw && !f.withdrawalSent){f.withdrawalSent=true;result.events=[];result.nextSequence=String(seq+1);result.removedEventIds=[current.id];}
          if(f.generation===1)f.oldCurrent=current;
          if(f.holdNext){f.holdNext=false;f.held=true;const saved=structuredClone(result);await new Promise(resolve=>f.releaseHeld=resolve);result=saved;}
        }
      } else if(method==='getLiveSpeciesCounts')result={sessionId:f.session?.id??'',sessionActive:!!f.session,revision:String(f.generation*100+2),species:f.session&&f.allowEvents&&!f.withdraw?[{speciesId:'current-'+f.generation,commonName:'Current bird '+f.generation,count:2400}]:[],nextCursor:null};
      else if(method==='getLiveOutcome')result={outcome:{eventId:args.eventId,speciesId:args.eventId,region:'Fixture receiver',status:'queued',voiceCharacter:'struck',representativeId:null}};
      else throw Error('Unexpected fixture call '+method);
      if(method==='getLiveChanges')Object.assign(result,{removedEventIds:result.removedEventIds??[],hasMore:result.hasMore??false});
      return {ok:true,requestId:request.requestId,result:structuredClone(result)};
    }};
    document.addEventListener('DOMContentLoaded',()=>{
      new MutationObserver(()=>{
        if(document.querySelector('[role="radio"][aria-checked="true"]')?.textContent!=='Live')return;
        const text=document.querySelector('.atlas')?.textContent??'';
        if(text.includes('Historical seal')||text.includes('Delayed pre-Collect bird'))f.leaks.push(text.slice(0,250));
      }).observe(document.documentElement,{subtree:true,childList:true,characterData:true});
    });
  });
  const collect=()=>page.getByRole('button',{name:/^(Start live collection|Live collection options)$/});
  const start=async()=>{await collect().click();await page.getByRole('dialog',{name:/live collection/i}).getByLabel('NZ shark reports',{exact:true}).uncheck();await page.getByRole('button',{name:'Start live updates',exact:true}).click();};
  const row=name=>page.getByRole('button',{name:`Show ${name} on map`,exact:true});
  await page.goto(origin);await collect().waitFor();
  const listening=()=>page.getByRole('switch',{name:/Live listening/});
  await page.getByRole('button',{name:'Live listening status',exact:true}).getByText('Start Collect to listen',{exact:true}).waitFor();
  assert.equal(await listening().isDisabled(),true,'Managed listening cannot start before a confirmed Collect session');
  await page.getByRole('button',{name:'Live listening status',exact:true}).click();
  assert.equal(await page.getByRole('dialog',{name:'Live listening status',exact:true}).isVisible(),true,'Status stays available before Collect');
  await page.keyboard.press('Escape');
  assert.equal(await page.evaluate(()=>sessionFixture.calls.filter(row=>row.method==='setLiveEnabled').length),0);
  await page.getByText('Start Collect to see observations from this collection session.',{exact:false}).waitFor();
  assert.equal(await page.locator('.species-row').count(),0);assert.equal(await page.locator('.latest-detections__row').count(),0);
  await start();await page.waitForFunction(()=>sessionFixture.session?.id==='collection-1');
  assert.equal(await page.locator('.species-row').count(),0,'Start does not expose the stored catalogue');
  await page.waitForFunction(()=>!document.querySelector('[role="switch"][aria-label^="Live listening"]')?.disabled);
  await listening().click();await page.getByRole('switch',{name:'Live listening (on)',exact:true}).waitFor();
  await page.evaluate(()=>{sessionFixture.allowEvents=true;});
  await row('Current bird 1').waitFor();await page.locator('.species-row').filter({hasText:'Current bird 1'}).getByText('2400',{exact:true}).waitFor();
  assert.equal(await page.getByText('Historical seal',{exact:true}).count(),0);assert.equal(await page.getByText('Delayed pre-Collect bird',{exact:true}).count(),0);
  await row('Current bird 1').click();await page.locator('.inspector').getByText('CURRENT BIRD 1',{exact:true}).waitFor();
  await page.locator('.inspector').getByText('Live musical outcome · queued',{exact:true}).waitFor();
  const baselineReads=await page.evaluate(()=>sessionFixture.calls.filter(row=>row.method==='getLiveChanges'&&row.args.afterSequence==='100').length);
  await page.evaluate(()=>{sessionFixture.failStop=true;});await collect().click();await page.getByRole('button',{name:'Pause updates',exact:true}).click();
  await page.getByRole('alert').filter({hasText:'Synthetic stop rejected'}).waitFor();await page.keyboard.press('Escape');
  await page.getByRole('tab',{name:'Latest detections',exact:true}).click();await row('Current bird 1').waitFor();
  assert.equal(await page.evaluate(()=>sessionFixture.calls.filter(row=>row.method==='getLiveChanges'&&row.args.afterSequence==='100').length),baselineReads,'A rejected stop preserves the revision cursor; arrivals cannot replay');
  await row('Current bird 1').click();
  await page.evaluate(()=>{sessionFixture.holdNext=true;});await page.waitForFunction(()=>sessionFixture.held);
  await collect().click();await page.getByRole('button',{name:'Pause updates',exact:true}).click();
  await page.getByText('Start Collect to see observations from this collection session.',{exact:false}).waitFor();
  assert.equal(await listening().isDisabled(),false,'Mute remains available while the last known listening state is on');
  await listening().click();await page.getByRole('switch',{name:'Live listening (off)',exact:true}).waitFor();
  assert.equal(await listening().isDisabled(),true,'Listening stays off while Collect is stopped');
  assert.equal(await page.locator('.species-row').count(),0);assert.equal(await page.locator('.inspector').count(),0);assert.equal(await page.locator('.grain__lanes [role="button"]').count(),0);
  await start();await page.waitForFunction(()=>sessionFixture.session?.id==='collection-2');
  await page.evaluate(()=>{sessionFixture.releaseHeld?.();sessionFixture.allowEvents=true;});
  await page.getByRole('tab',{name:'Latest detections',exact:true}).click();await row('Current bird 2').waitFor();
  assert.equal(await page.getByText('Current bird 1',{exact:true}).count(),0,'Old in-flight and stored events cannot cross restart');
  await page.getByRole('radio',{name:'History',exact:true}).click();await page.getByText('Historical seal',{exact:true}).waitFor();
  await page.getByRole('radio',{name:'Live',exact:true}).click();await row('Current bird 2').waitFor();
  assert.equal(await page.getByText('Historical seal',{exact:true}).count(),0);
  await mkdir(output,{recursive:true});await page.screenshot({path:resolve(output,'strict-live-session.png')});
  await row('Current bird 2').click();await page.locator('.inspector').getByText('CURRENT BIRD 2',{exact:true}).waitFor();
  await page.evaluate(()=>{sessionFixture.withdraw=true;});
  await page.waitForFunction(()=>!document.querySelector('.inspector') && !document.querySelector('.species-row'));
  assert.equal(await page.locator('.grain__lane').count(),0,'Correction withdrawal clears ranking and Grain');
  await page.evaluate(()=>{sessionFixture.session=null;sessionFixture.sessionError='Collection stopped because its library changed.';});
  await page.getByRole('alert').filter({hasText:'Collection stopped because its library changed.'}).waitFor();
  const continuation=await page.evaluate(()=>{
    const pages=sessionFixture.calls.filter(row=>row.method==='getLiveChanges' && row.args.sessionId==='collection-1');
    const next=pages.find(row=>row.args.afterSequence==='101');
    const prior=pages.filter(row=>row.at<=next.at && row.args.afterSequence==='100').at(-1);
    return next.at-prior.at;
  });assert.ok(continuation<1000,'Filtered empty pages continue immediately');
  const evidence=await page.evaluate(()=>({calls:sessionFixture.calls.reduce((out,row)=>(out[row.method]=(out[row.method]??0)+1,out),{}),leaks:sessionFixture.leaks}));
  assert.equal(evidence.calls.getChanges??0,0,'Native Live never loads the historical baseline');assert.deepEqual(evidence.leaks,[]);assert.deepEqual(errors,[]);
  await page.screenshot({path:resolve(output,'withdrawn-session.png')});await writeFile(resolve(output,'result.json'),JSON.stringify({status:'passed',...evidence,continuationMs:continuation,errors,synthetic:true,audioDeviceOpened:false},null,2));console.log(JSON.stringify({status:'passed',...evidence,output}));
}finally{await browser?.close();server.kill('SIGTERM');}
