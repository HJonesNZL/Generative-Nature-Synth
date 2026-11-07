// Isolated native-session fixture, no owner library/provider/audio access.
import assert from 'node:assert/strict';
const { chromium } = await import(process.env.PLAYWRIGHT_MODULE || 'playwright');
const origin = process.env.ATLAS_TEST_URL || 'http://127.0.0.1:5187';
const browser = await chromium.launch({ headless: true, channel: 'chrome', args: ['--enable-unsafe-swiftshader'] });
const page = await browser.newPage({viewport:{width:1440,height:900},reducedMotion:'reduce'});
const errors=[];
page.on('pageerror',error=>errors.push(error.message));
await page.addInitScript(()=>{
  const stamp=new Date(Date.now()-30000).toISOString();
  const session={id:'grain-session',storeId:'grain-store',baselineRevision:'10',observedFromUtc:new Date(Date.now()-60000).toISOString()};
  const rows=Array.from({length:300},(_,i)=>({speciesId:`species-${String(i).padStart(3,'0')}`,commonName:`Fixture species ${String(i).padStart(3,'0')}`,count:300-i}));
  const fixture=window.grainFixture={rows,revision:'12',countCalls:0,collecting:true};
  const events=[0,299].map((i,index)=>({id:`event-${i}`,revisionSequence:String(11+index),sourceId:'birdweather',speciesId:rows[i].speciesId,commonName:rows[i].commonName,stationId:'fixture-station',eventKind:'acoustic_species_detection',locationKind:'station_detection',position:{latitude:-41,longitude:174,uncertaintyMetres:null},observedAtUtc:stamp,firstSeenAtUtc:stamp,confidence:null,attribution:'Synthetic regression fixture',qualityFlags:[],provenance:'live_source'}));
  const methods=['getCapabilities','getLiveSources','getLiveChanges','getLiveSpeciesCounts','getCatalog','getLiveState','setLiveEnabled','getAudioState'];
  window.atlasHost={request:async request=>{
    const {method,args}=request;let result;
    if(method==='getCapabilities') result={protocolVersion:1,implementation:'native',supportedMethods:methods,audioAvailable:true,libraryAvailable:true,maxEventBatch:500,maxVisibleMarkers:400};
    else if(method==='getLiveState'||method==='setLiveEnabled') result={enabled:false,status:'off',received:300,admitted:0,dropped:0,skippedRevisions:0,grouped:0,error:'',mode:'smoothed',density:3,pending:0,catchingUp:false};
    else if(method==='getAudioState') result={status:'disabled',deviceName:null,masterGainDb:-18,peakDbfs:null,meterIsSimulated:false,clipping:false,activeSynthVoices:0,maxSynthVoices:8};
    else if(method==='getLiveSources') result={collecting:fixture.collecting,libraryActive:true,session:fixture.collecting?session:null,sources:[{id:'nzsharks',enabled:false,polling:false,lastAttemptUtc:null,lastSuccessUtc:null,lastObservationUtc:null,error:null,records:'0',complete:false,gap:false,nextPollSeconds:0},{id:'birdweather',enabled:fixture.collecting,polling:false,lastAttemptUtc:stamp,lastSuccessUtc:stamp,lastObservationUtc:stamp,error:null,records:'100000',complete:true,gap:false,nextPollSeconds:60}]};
    else if(method==='getLiveChanges') result={storeId:'grain-store',sessionId:session.id,sessionActive:fixture.collecting,nextSequence:'12',events:args.afterSequence==='10'?events.map(event=>({event,liveEligible:true,change:'new'})):[]};
    else if(method==='getLiveSpeciesCounts') {
      fixture.countCalls++;
      const ranked=[...fixture.rows].sort((a,b)=>b.count-a.count||a.speciesId.localeCompare(b.speciesId));
      const start=Number(args.cursor??0),end=Math.min(ranked.length,start+(args.limit??250));
      result={sessionId:session.id,sessionActive:fixture.collecting,revision:fixture.revision,species:ranked.slice(start,end),nextCursor:end<ranked.length?String(end):null};
    } else if(method==='getCatalog') result={species:[rows[0],rows[299]].map(row=>({id:row.speciesId,commonName:row.commonName,scientificName:'',group:'bird'})),stations:[{id:'fixture-station',name:'Fixture station',latitude:-41,longitude:174,sourceId:'birdweather',positionDescription:'Synthetic fixture'}],sources:[{id:'nzsharks',enabled:false,polling:false,lastAttemptUtc:null,lastSuccessUtc:null,lastObservationUtc:null,error:null,records:'0',complete:false,gap:false,nextPollSeconds:0},{id:'birdweather',name:'BirdWeather fixture'}],limited:true};
    else throw Error('Unexpected Grain fixture method: '+method);
    return {requestId:request.requestId,ok:true,result:structuredClone(result)};
  }};
});
try {
  await page.goto(origin);
  const region=page.getByRole('region',{name:'Species ranked by detection count',exact:true});
  const rows=region.locator('.grain__lane');
  await page.waitForFunction(()=>document.querySelectorAll('.grain__scroll .grain__lane').length===300);
  await page.evaluate(()=>window.scrollTo(0,document.documentElement.scrollHeight));
  assert.equal(await page.evaluate(()=>window.scrollY),0,'whole document cannot scroll to blank space beneath the app');
  assert.equal(await rows.count(),300,'every species is available beyond retained map/catalogue samples');
  assert.equal(await rows.first().locator('.grain__count').innerText(),'300');
  assert.equal(await rows.nth(150).locator('path').first().getAttribute('d'),'','species without retained ticks keeps its full numeric count');
  assert.ok(await region.evaluate(element=>element.scrollHeight>element.clientHeight),'Grain has its own bounded scroll area');
  const mainScroll=await page.evaluate(()=>document.scrollingElement.scrollTop);
  await region.hover();await page.mouse.wheel(0,10000);
  await page.waitForFunction(()=>document.querySelector('.grain__scroll').scrollTop>0);
  assert.equal(await page.evaluate(()=>document.scrollingElement.scrollTop),mainScroll,'scrolling Grain does not scroll the whole app');
  await rows.last().locator('button').scrollIntoViewIfNeeded();
  await rows.last().locator('button').click();
  assert.equal(await rows.last().locator('button').getAttribute('aria-pressed'),'true','scrolled species remains usable');
  assert.equal(await rows.last().locator('.grain__count').innerText(),'1');
  await page.evaluate(()=>{window.grainFixture.rows[299].count=901;window.grainFixture.revision='13';});
  await page.waitForFunction(()=>document.querySelector('.grain__scroll .grain__name')?.textContent?.includes('299'));
  assert.equal(await rows.first().locator('.grain__count').innerText(),'901','count update re-ranks live session without relying on map retention');
  assert.ok(await page.evaluate(()=>window.grainFixture.countCalls>=4),'full paged ranking refreshed');
  await page.getByRole('button',{name:'Sound Studio',exact:true}).click();
  await page.waitForFunction(()=>document.querySelectorAll('.grain__scroll .grain__lane').length===300);
  assert.ok(await region.evaluate(element=>element.scrollHeight>element.clientHeight),'Studio keeps an independent Grain scroller');
  await page.setViewportSize({width:1280,height:800});
  assert.ok(await region.evaluate(element=>element.clientHeight>20&&element.scrollHeight>element.clientHeight),'supported smaller desktop keeps rows and scrolling usable');
  await page.evaluate(()=>window.scrollTo(0,document.documentElement.scrollHeight));
  assert.equal(await page.evaluate(()=>window.scrollY),0,'smaller wrapped layout cannot scroll into blank page');
  assert.ok((await page.locator('.app-header').boundingBox()).height>80,'1280px fixture exercises wrapped header');
  await page.getByRole('button',{name:'Live listening status',exact:true}).click();
  const dialog=page.getByRole('dialog',{name:'Live listening status',exact:true});
  await dialog.waitFor();
  const bounds=await dialog.boundingBox();
  assert.ok(bounds.x>=0&&bounds.y>=0&&bounds.x+bounds.width<=1280&&bounds.y+bounds.height<=800,'listening menu stays inside all four viewport edges');
  await page.keyboard.press('Escape');
  assert.equal(await dialog.count(),0,'Escape dismisses the listening status menu');
  await page.screenshot({path:process.env.ATLAS_GRAIN_SCREENSHOT||'/tmp/atlas-grain-ranking.png'});
  assert.deepEqual(errors,[]);
  console.log(JSON.stringify({checks:['300 full-session ranked species from two retained events','independent scrolling','numeric counts','scrolled row highlight','live count re-ranking','Atlas and Studio','1440x900 and 1280x800'],errors}));
} catch(error) {
  console.error(JSON.stringify(await page.evaluate(()=>({errors:[],scrollY:window.scrollY,metrics:['html','body','#root','.app','.app-workspace','.screen','.atlas__left','.grain','.grain__lanes','.grain__scroll'].map(selector=>{const node=document.querySelector(selector);if(!node)return{selector,missing:true};const style=getComputedStyle(node),rect=node.getBoundingClientRect();return{selector,outerHeight:rect.height,clientHeight:node.clientHeight,scrollHeight:node.scrollHeight,overflow:style.overflow,display:style.display,flex:style.flex,minHeight:style.minHeight,top:rect.top,bottom:rect.bottom};}),deepest:[...document.querySelectorAll('body *')].map(node=>({tag:node.tagName,className:node.className?.baseVal??node.className,parentClass:node.parentElement?.className?.baseVal??node.parentElement?.className,bottom:node.getBoundingClientRect().bottom})).sort((a,b)=>b.bottom-a.bottom).slice(0,3)}))));
  throw error;
} finally {await browser.close();}
