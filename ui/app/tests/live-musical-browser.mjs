// Disposable deterministic native-like fixture. Never opens audio or an owner database.
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {mkdir, writeFile} from 'node:fs/promises';
import {resolve} from 'node:path';
const {chromium}=await import(process.env.PLAYWRIGHT_MODULE || 'playwright');
const port=Number(process.env.ATLAS_MUSICAL_UI_PORT || 5197), origin=`http://127.0.0.1:${port}`;
const output=resolve('test-results/live-musical');
const server=spawn(process.execPath,['node_modules/vite/bin/vite.js','--port',String(port),'--strictPort'],{env:{...process.env,ATLAS_LIBRARY:''},stdio:['ignore','pipe','pipe']});
server.stdout.resume();server.stderr.resume();
let browser;
try {
  for(let n=0;n<100;n++){try{if((await fetch(origin)).ok)break;}catch{}await new Promise(r=>setTimeout(r,50));}
  browser=await chromium.launch({headless:true,channel:'chrome',args:['--enable-unsafe-swiftshader']});
  const page=await browser.newPage({viewport:{width:1440,height:900},reducedMotion:'reduce'});
  const errors=[];page.on('pageerror',e=>errors.push(e.message));page.setDefaultTimeout(10000);
  await page.addInitScript(()=>{
    const stamp=new Date().toISOString();
    const event={id:'musical-event-1',revisionSequence:'1',sourceId:'fixture',speciesId:'fixture-bird',commonName:'Fixture bird',individualId:null,deploymentId:null,stationId:'receiver',eventKind:'acoustic_species_detection',locationKind:'station_detection',position:{latitude:-38,longitude:176,uncertaintyMetres:null},observedAtUtc:stamp,firstSeenAtUtc:stamp,confidence:null,attribution:'Synthetic UI qualification',qualityFlags:[],provenance:'demo'};
    const f=window.musicalFixture={peak:-18,calls:[],live:{enabled:false,status:'off',received:1,admitted:1,dropped:0,skippedRevisions:0,grouped:0,error:'',mode:'direct',density:3,composition:{enabled:true,tempoBpm:90,tonicPitchClass:0,scale:'majorPentatonic',density:1,variation:.25,seed:1,quantize:true},spatial:{enabled:false,amount:.25,regions:[]}},outcome:{eventId:event.id,speciesId:event.speciesId,region:'New Zealand',status:'sounded',voiceCharacter:'whistle',representativeId:null}};
    const methods=['getCapabilities','getCatalog','getChanges','getLiveState','setLiveEnabled','setLiveSettings','getLiveOutcome','getAudioState'];
    window.atlasHost={request:async request=>{
      const {method,args}=request;f.calls.push({method,args});let result;
      if(method==='getCapabilities')result={protocolVersion:1,implementation:'native',supportedMethods:methods,audioAvailable:true,supportsSmoothedLive:true,libraryAvailable:true,maxEventBatch:500,maxVisibleMarkers:400};
      else if(method==='getCatalog')result={species:[{id:'fixture-bird',commonName:'Fixture bird',scientificName:'Synthetic species',group:'bird'}],stations:[{id:'receiver',name:'Fixture receiver',sourceId:'fixture',latitude:-38,longitude:176,positionDescription:'Test'}],sources:[{id:'fixture',name:'Synthetic fixture'}],limited:false};
      else if(method==='getChanges')result={storeId:'musical-ui',nextSequence:'1',events:args.afterSequence==='0'?[{event,liveEligible:false,change:'baseline'}]:[]};
      else if(method==='getAudioState')result={status:f.live.enabled?'running':'disabled',deviceName:null,masterGainDb:-18,peakDbfs:f.live.enabled?f.peak:null,meterIsSimulated:false,clipping:false,activeSynthVoices:0,maxSynthVoices:24};
      else if(method==='getLiveOutcome')result={outcome:f.outcome};
      else {if(method==='setLiveEnabled'){f.live.enabled=args.enabled;f.live.status=args.enabled?'listening':'off';}if(method==='setLiveSettings')Object.assign(f.live,args);result=f.live;}
      return {ok:true,requestId:request.requestId,result:structuredClone(result)};
    }};
  });
  await page.goto(origin);await page.getByRole('switch',{name:'Live listening (off)',exact:true}).waitFor();
  await page.getByRole('button',{name:'Live listening status',exact:true}).click();
  assert.equal(await page.getByLabel('Live mode',{exact:true}).inputValue(),'direct','startup stays Direct');
  await page.getByRole('option',{name:'Smoothed (recommended)',exact:true}).waitFor({state:'attached'});
  await page.getByLabel('Live mode',{exact:true}).selectOption('smoothed');
  await page.waitForFunction(()=>musicalFixture.live.mode==='smoothed');
  await page.evaluate(()=>Object.assign(musicalFixture.live,{pending:5,admitted:1,catchingUp:true}));
  await page.getByText(/1 sounded · 5 pending · catching up with arrivals/).waitFor();
  assert.equal(await page.getByLabel('Live density',{exact:true}).count(),0);
  await page.getByLabel('Live mode',{exact:true}).selectOption('musical');
  await page.waitForFunction(()=>musicalFixture.live.mode==='musical');
  await page.getByLabel('Live density',{exact:true}).selectOption('6');
  await page.waitForFunction(()=>musicalFixture.live.density===6);
  await page.getByRole('button',{name:'Edit live voices, key & regions',exact:true}).click();
  await page.getByText('Live key & scale',{exact:true}).click();
  await page.locator('.studio').getByLabel('Live tonic',{exact:true}).selectOption('7');
  await page.locator('.studio').getByLabel('Live scale',{exact:true}).selectOption('naturalMinor');
  await page.locator('.studio').getByRole('button',{name:'Apply live composition',exact:true}).click();
  await page.waitForFunction(()=>musicalFixture.live.composition.tonicPitchClass===7&&musicalFixture.live.composition.scale==='naturalMinor');
  await page.getByLabel('Enable spatial mixing',{exact:true}).check();
  await page.getByLabel('Global ambience amount',{exact:true}).fill('0.65');
  await page.waitForTimeout(2200);
  assert.equal(await page.getByLabel('Global ambience amount',{exact:true}).inputValue(),'0.65','live polls preserve unsaved region draft');
  await page.getByRole('button',{name:'Apply spatial settings',exact:true}).click();
  await page.waitForFunction(()=>musicalFixture.live.spatial.enabled&&musicalFixture.live.spatial.amount===.65);
  await page.getByRole('switch',{name:'Live listening (off)',exact:true}).click();
  await page.getByRole('switch',{name:'Live listening (on)',exact:true}).waitFor();
  await page.getByRole('img',{name:'Output peak -18 dBFS',exact:true}).waitFor();
  assert.equal(await page.locator('.meter__seg.is-lit').count(),6);
  const meterStart=await page.evaluate(()=>musicalFixture.calls.filter(c=>c.method==='getAudioState').length);
  await page.evaluate(()=>{musicalFixture.peak=-30;});
  await page.getByRole('img',{name:'Output peak -30 dBFS',exact:true}).waitFor();
  assert.equal(await page.locator('.meter__seg.is-lit').count(),4);
  await page.waitForTimeout(1100);
  const meterPolls=await page.evaluate(()=>musicalFixture.calls.filter(c=>c.method==='getAudioState').length)-meterStart;
  assert.ok(meterPolls<=9,`meter polling is finite: ${meterPolls}`);
  await page.getByRole('button',{name:'Atlas',exact:true}).click();
  await page.getByRole('list',{name:'Latest detections by observation time',exact:true}).getByRole('button').first().click();
  const inspector=page.locator('.inspector');await inspector.getByText('Live musical outcome · sounded',{exact:true}).click();
  await inspector.getByText('New Zealand',{exact:true}).waitFor();
  await inspector.getByText('whistle',{exact:true}).waitFor();
  await page.evaluate(()=>{musicalFixture.outcome.status='grouped';musicalFixture.outcome.representativeId='musical-event-0';});
  await inspector.getByText('Live musical outcome · grouped',{exact:true}).waitFor();
  await inspector.getByText('musical-event-0',{exact:true}).waitFor();
  await inspector.getByText('whistle',{exact:true}).waitFor();
  assert.equal(await inspector.locator('details').filter({hasText:'Phrase representative'}).evaluate(e=>e.open),true,'polling preserves disclosure state');
  await page.evaluate(()=>{musicalFixture.outcome.status='skipped';musicalFixture.outcome.representativeId=null;});
  await inspector.getByText('Live musical outcome · skipped',{exact:true}).waitFor();
  const livePollStart=await page.evaluate(()=>musicalFixture.calls.filter(c=>c.method==='getLiveState').length);
  const count=await page.evaluate(()=>musicalFixture.calls.filter(c=>c.method==='getLiveOutcome').length);
  await page.waitForTimeout(2200);
  const next=await page.evaluate(()=>musicalFixture.calls.filter(c=>c.method==='getLiveOutcome').length);
  const livePolls=await page.evaluate(()=>musicalFixture.calls.filter(c=>c.method==='getLiveState').length)-livePollStart;
  assert.ok(livePolls<=3,`one consolidated live poll: ${livePolls}`);
  assert.ok(next-count<=4,`bounded outcome polling, observed ${next-count}`);
  await page.getByRole('button',{name:'Live listening status',exact:true}).click();
  await page.getByLabel('Live mode',{exact:true}).selectOption('smoothed');
  await page.waitForFunction(()=>musicalFixture.live.mode==='smoothed');
  await page.getByRole('button',{name:'Live listening status',exact:true}).click();
  await mkdir(output,{recursive:true});const checks=[];
  for(const viewport of [{width:1440,height:900},{width:1280,height:800}]){
    await page.setViewportSize(viewport);
    for(const screen of ['Atlas','Sound Studio']){
      await page.getByRole('button',{name:screen,exact:true}).click();
      assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false,`${screen} fits ${viewport.width}`);
      const header=await page.locator('header').evaluate(e=>({scroll:e.scrollWidth,width:e.clientWidth}));
      assert.ok(header.scroll<=header.width+1,`header fits ${viewport.width}`);
      if(screen==='Sound Studio') {
        await page.getByText('Live key & scale',{exact:true}).click();
        await page.locator('.studio').getByText('Smoothed uses each species voice and pitch.',{exact:false}).waitFor();
        assert.equal(await page.getByLabel('Live tempo BPM',{exact:true}).count(),0);
        assert.equal(await page.getByLabel('Live tonic',{exact:true}).count(),0);
        assert.equal(await page.locator('.studio').getByLabel('Enable spatial mixing',{exact:true}).count(),1);
      }
      await page.screenshot({path:resolve(output,`${screen==='Atlas'?'atlas':'studio'}-${viewport.width}.png`)});
      checks.push({screen,...viewport,header});
    }
  }
  assert.deepEqual(errors,[]);assert.equal(await page.evaluate(()=>musicalFixture.calls.filter(c=>c.method==='setLiveEnabled').length),1);
  const calls=await page.evaluate(()=>musicalFixture.calls.reduce((r,c)=>(r[c.method]=(r[c.method]||0)+1,r),{}));
  await writeFile(resolve(output,'result.json'),JSON.stringify({checks,errors,calls,meterPolls,livePolls,synthetic:true,audioDeviceOpened:false},null,2));
  console.log(JSON.stringify({result:'passed',checks,calls,output}));
}finally{await browser?.close();server.kill('SIGTERM');}
