// Disposable actual-onset journal fixture; no audio, provider or owner database.
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {mkdir,writeFile} from 'node:fs/promises';
import {resolve} from 'node:path';
const {chromium}=await import(process.env.PLAYWRIGHT_MODULE || 'playwright');
const port=Number(process.env.ATLAS_RIBBON_UI_PORT||5201),origin=`http://127.0.0.1:${port}`,output=resolve('test-results/time-ribbon');
const server=spawn(process.execPath,['node_modules/vite/bin/vite.js','--port',String(port),'--strictPort'],{env:{...process.env,ATLAS_LIBRARY:''},stdio:['ignore','pipe','pipe']});server.stdout.resume();server.stderr.resume();let browser;
try {
  for(let n=0;n<100;n++){try{if((await fetch(origin)).ok)break;}catch{}await new Promise(r=>setTimeout(r,50));}
  browser=await chromium.launch({headless:true,channel:'chrome',args:['--enable-unsafe-swiftshader']});
  const page=await browser.newPage({viewport:{width:1440,height:900},reducedMotion:'reduce'});const errors=[];page.on('pageerror',e=>errors.push(e.message));page.setDefaultTimeout(15000);
  await page.addInitScript(()=>{
    const start=new Date(Date.now()-60000).toISOString();
    const f=window.ribbonFixture={collecting:true,updates:[],sequence:0,calls:[],leaks:[],jumpUs:0,clock:()=>String(Math.round(performance.now()*1000)+1_000_000_000+f.jumpUs),put:voice=>f.updates.push({seq:++f.sequence,voice})};
    const session={id:'ribbon-collection',storeId:'ribbon-store',baselineRevision:'10',observedFromUtc:start};
    const rows=Array.from({length:300},(_,i)=>({speciesId:`species-${i}`,commonName:`Ranked species ${String(i).padStart(3,'0')}`,count:300-i}));
    const source=id=>({id,enabled:f.collecting&&id==='birdweather',polling:false,lastAttemptUtc:null,lastSuccessUtc:null,lastObservationUtc:null,records:'0',error:null,complete:false,gap:false,nextPollSeconds:60});
    const methods=['getCapabilities','getLiveSources','setLiveSources','getLiveChanges','getLiveSpeciesCounts','getCatalog','getLiveActivity'];
    window.atlasHost={request:async request=>{
      const {method,args}=request;f.calls.push({method,args,at:performance.now()});let result;
      if(method==='getCapabilities')result={protocolVersion:1,implementation:'native',supportedMethods:methods,audioAvailable:false,libraryAvailable:true,maxEventBatch:500,maxVisibleMarkers:400};
      else if(method==='getLiveSources'||method==='setLiveSources'){if(method==='setLiveSources')f.collecting=args.sourceIds.length>0;result={collecting:f.collecting,libraryActive:true,session:f.collecting?session:null,sources:[source('birdweather'),source('nzsharks')]};}
      else if(method==='getCatalog')result={species:[],stations:[],sources:[],limited:false};
      else if(method==='getLiveChanges')result={sessionId:session.id,sessionActive:f.collecting,storeId:session.storeId,nextSequence:'12',removedEventIds:[],hasMore:false,events:args.afterSequence==='10'?[0,1].map(index=>({change:'new',liveEligible:true,event:{id:'arrival-'+index,revisionSequence:String(11+index),sourceId:'birdweather',speciesId:'species-'+index,commonName:'Arrived, not sounded '+index,individualId:null,deploymentId:null,stationId:'receiver',eventKind:'acoustic_species_detection',locationKind:'station_detection',position:{latitude:-41,longitude:174,uncertaintyMetres:null},observedAtUtc:new Date().toISOString(),firstSeenAtUtc:new Date().toISOString(),confidence:null,attribution:'Synthetic fixture',qualityFlags:[],provenance:'live_source'}})):[]};
      else if(method==='getLiveSpeciesCounts'){const at=Number(args.cursor??0),end=Math.min(rows.length,at+(args.limit??250));result={sessionId:session.id,sessionActive:f.collecting,revision:'12',species:f.collecting?rows.slice(at,end):[],nextCursor:f.collecting&&end<rows.length?String(end):null};}
      else if(method==='getLiveActivity'){
        const changes=f.updates.filter(row=>row.seq>Number(args.afterSequence??0)),batch=changes.slice(0,1);
        await new Promise(resolve=>setTimeout(resolve,5));
        result={sessionId:session.id,sessionActive:f.collecting,clockUs:f.clock(),voices:f.collecting?batch.map(row=>row.voice):[],dropped:0,nextSequence:batch.length?String(batch.at(-1).seq):String(f.sequence),hasMore:f.collecting&&changes.length>batch.length,reset:false};
      }
      else throw Error('Unexpected fixture method '+method);
      return {ok:true,requestId:request.requestId,result:structuredClone(result)};
    }};
    document.addEventListener('DOMContentLoaded',()=>new MutationObserver(()=>{
      if([...document.querySelectorAll('.time-ribbon__voice')].some(node=>node.textContent.includes('History-only bird')))f.leaks.push('Historical start delta falsely appeared as sounding');
    }).observe(document.documentElement,{subtree:true,childList:true,characterData:true}));
  });
  await page.goto(origin);await page.getByRole('tab',{name:'Playback · 30 seconds',exact:true}).waitFor();
  await page.getByRole('list',{name:'Now sounding',exact:true}).waitFor();
  assert.equal(await page.locator('.time-ribbon__note').count(),0,'Arriving detections do not create playback marks');
  await page.evaluate(()=>{
    const f=ribbonFixture,onset=f.clock();
    const note=(token,patch={})=>({token,eventId:'actual-unretained-'+token,speciesId:'tui',commonName:'Tūī',voiceName:'Resonant pluck',voiceType:'pluck',position:{latitude:-41,longitude:174,uncertaintyMetres:1200},stationId:'birdweather:station:fixture',eventKind:'acoustic_species_detection',locationKind:'station_detection',locationLabel:'Acoustic receiver location',region:null,onsetUs:onset,endUs:null,...patch});
    f.put(note('render:1'));f.put(note('render:2'));
    f.put(note('render:3',{speciesId:'seal',commonName:'Spotted seal',voiceName:'Air',voiceType:'air',position:null,stationId:null,eventKind:'satellite_fix',locationKind:'animal_location',locationLabel:'Location withheld',region:'Do not reveal this region'}));
  });
  await page.waitForFunction(()=>document.querySelectorAll('.time-ribbon__voice').length===3);
  assert.equal(await page.locator('[data-activity-token="render:1"]').getAttribute('data-location-label'),'Receiver · 41° S, 174° E · ±1.2 km');
  const privateNote=page.locator('[data-activity-token="render:3"]');assert.equal(await privateNote.getAttribute('data-location-label'),'Measured fix · location withheld');
  assert.equal(await page.locator('.time-ribbon').getByText('Do not reveal this region',{exact:false}).count(),0);
  assert.equal(await page.locator('.time-ribbon__voice').filter({hasText:'Resonant pluck'}).count(),2,'Every simultaneous actual note remains named');
  await page.locator('[data-activity-token="render:2"]').click();await page.getByLabel('Choose playback note',{exact:true}).selectOption('render:1');
  assert.equal(await page.locator('[data-activity-token="render:1"]').getAttribute('aria-pressed'),'true','Mouse chooser reaches an obscured overlapping note');
  await page.locator('[data-activity-token="render:1"]').focus();await page.keyboard.press('ArrowRight');assert.equal(await page.locator('[data-activity-token="render:2"]').getAttribute('aria-pressed'),'true');
  await page.getByRole('tab',{name:'Playback · 30 seconds',exact:true}).focus();await page.keyboard.press('ArrowRight');
  const ranking=page.getByRole('region',{name:'Species ranked by detection count',exact:true});await ranking.waitFor();assert.equal(await ranking.locator('.grain__lane').count(),300);
  await ranking.getByRole('button').last().scrollIntoViewIfNeeded();assert.ok(await ranking.evaluate(node=>node.scrollTop)>0,'Complete ranking scrolls independently');assert.equal(await page.evaluate(()=>scrollY),0);
  await page.getByRole('tab',{name:'Species · session ranking',exact:true}).focus();await page.keyboard.press('ArrowLeft');
  await page.evaluate(()=>{
    const f=ribbonFixture,now=Number(f.clock());const v={...f.updates[0].voice,token:'render:4',commonName:'History-only bird',speciesId:'past',onsetUs:String(now-2_000_000),endUs:null};f.put(v);f.put({...v,endUs:String(now-1_000_000)});
  });
  await page.locator('[data-activity-token="render:4"]').waitFor();assert.equal(await page.locator('.time-ribbon__voice').filter({hasText:'History-only bird'}).count(),0,'Paginated historical start/end updates publish as one settled frontier');
  const checks=[];await mkdir(output,{recursive:true});
  for(const viewport of [{width:1440,height:900},{width:1280,height:800}]){
    await page.setViewportSize(viewport);
    for(const screen of ['Atlas','Sound Studio']){
      await page.getByRole('button',{name:screen,exact:true}).click();
      await page.getByRole('tab',{name:'Playback · 30 seconds',exact:true}).waitFor();
      assert.equal(await page.getByRole('tab',{name:'Playback · 30 seconds',exact:true}).getAttribute('aria-selected'),'true','Activity tab persists across screens');
      assert.equal(await page.locator('.time-ribbon__voice').count(),3);
      assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);
      const bounds=await page.locator('.time-ribbon').boundingBox();assert.ok(bounds.height>50&&bounds.y+bounds.height<=viewport.height+1,screen+' ribbon fits');
      await page.screenshot({path:resolve(output,`${screen==='Atlas'?'atlas':'studio'}-${viewport.width}.png`)});checks.push({screen,...viewport,bounds});
    }
  }
  await page.evaluate(()=>{const f=ribbonFixture;for(const token of ['render:1','render:2','render:3']){const original=[...f.updates].reverse().find(row=>row.voice.token===token).voice;f.put({...original,endUs:f.clock()});}});
  await page.waitForFunction(()=>document.querySelectorAll('.time-ribbon__voice').length===0);assert.equal(await page.locator('.time-ribbon__note').count(),4,'Mute/end retains real thirty-second history');
  await page.evaluate(()=>{ribbonFixture.jumpUs=40_000_000;});await page.waitForFunction(()=>document.querySelectorAll('.time-ribbon__note').length===0);
  await page.getByRole('button',{name:'Live collection options',exact:true}).click();await page.getByRole('button',{name:'Pause updates',exact:true}).click();await page.getByText('Start Collect to see playback.',{exact:true}).waitFor();
  const receipt=await page.evaluate(()=>({leaks:ribbonFixture.leaks,activityCalls:ribbonFixture.calls.filter(row=>row.method==='getLiveActivity').length}));
  assert.deepEqual(receipt.leaks,[]);assert.deepEqual(errors,[]);
  await writeFile(resolve(output,'result.json'),JSON.stringify({status:'passed',checks,...receipt,errors,synthetic:true,audioDeviceOpened:false},null,2));console.log(JSON.stringify({status:'passed',checks,...receipt,output}));
}finally{await browser?.close();server.kill('SIGTERM');}
