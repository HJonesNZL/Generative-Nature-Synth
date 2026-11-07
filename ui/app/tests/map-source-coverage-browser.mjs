// Synthetic host only; no owner state, provider requests or audio.
import assert from 'node:assert/strict';
const {chromium}=await import(process.env.PLAYWRIGHT_MODULE || 'playwright');
const origin=process.env.ATLAS_TEST_URL || 'http://127.0.0.1:5173';
const browser=await chromium.launch({headless:true,channel:'chrome',args:['--enable-unsafe-swiftshader']});
const page=await browser.newPage({viewport:{width:1440,height:900},reducedMotion:'reduce'});
const errors=[],external=[];page.on('pageerror',e=>errors.push(e.message));page.on('request',r=>{if(/^https?:/.test(r.url())&&new URL(r.url()).origin !== new URL(origin).origin)external.push(r.url())});
await page.addInitScript(()=>{
 const now=Date.now()-10000;
 const base={revisionSequence:'1',sourceId:'busy',speciesId:'bird',commonName:'Busy bird',stationId:null,eventKind:'acoustic_species_detection',locationKind:'station_detection',position:{latitude:-41,longitude:174,uncertaintyMetres:null},observedAtUtc:new Date(now).toISOString(),firstSeenAtUtc:new Date(now).toISOString(),confidence:null,attribution:'Synthetic fixture',qualityFlags:[],provenance:'demo'};
 const events=Array.from({length:170},(_,i)=>({...base,id:`busy-${i}`,stationId:`station-${i}`,revisionSequence:String(i+4),observedAtUtc:new Date(now+i).toISOString()}));
 events.unshift({...base,id:'report-nz',revisionSequence:'1',sourceId:'quiet-reports',speciesId:'report',commonName:'Quiet report',eventKind:'human_observation',locationKind:'reported_location',position:{latitude:-46.4,longitude:168.35,uncertaintyMetres:250},observedAtUtc:new Date(now-100000).toISOString()},{...base,id:'report-europe',revisionSequence:'2',sourceId:'quiet-reports',speciesId:'report',commonName:'Quiet report',eventKind:'human_observation',locationKind:'reported_location',position:{latitude:51,longitude:0,uncertaintyMetres:500},observedAtUtc:new Date(now-90000).toISOString()},{...base,id:'fix',revisionSequence:'3',sourceId:'quiet-track',speciesId:'whale',commonName:'Quiet whale',eventKind:'gps_fix',locationKind:'individual_fix',individualId:'genuine-fixture-tag',position:{latitude:40,longitude:-70,uncertaintyMetres:30},observedAtUtc:new Date(now-80000).toISOString()});
 const methods=['getCapabilities','getChanges','getCatalog','getSourceHealth','listRegions'];
 window.atlasHost={request:async q=>{let result;if(q.method==='getCapabilities')result={protocolVersion:1,implementation:'native',supportedMethods:methods,audioAvailable:false,libraryAvailable:true,maxEventBatch:500,maxVisibleMarkers:20};else if(q.method==='getChanges')result={storeId:'coverage-fixture',nextSequence:'173',...(q.args.afterSequence==='0'?{baselineTotal:2173}:{}),events:q.args.afterSequence==='0'?events.map(event=>({event,liveEligible:false,change:'baseline'})):[]};else if(q.method==='getCatalog')result={species:[{id:'bird',commonName:'Busy bird',scientificName:'',group:'bird'},{id:'report',commonName:'Quiet report',scientificName:'',group:'unclassified'},{id:'whale',commonName:'Quiet whale',scientificName:'',group:'mammal'}],stations:events.filter(e=>e.stationId).map(e=>({id:e.stationId,name:e.stationId,sourceId:e.sourceId,...e.position,positionDescription:'Synthetic receiver'})),sources:[{id:'busy',name:'Busy fixture'},{id:'quiet-reports',name:'Quiet report source'},{id:'quiet-track',name:'Quiet tracking source'}],limited:false};else if(q.method==='getSourceHealth'||q.method==='listRegions')result=[];else throw Error(q.method);return {requestId:q.requestId,ok:true,result:structuredClone(result)}}};
});
try{
 await page.goto(origin);
 await page.locator('.globe').waitFor();
 const list=page.getByRole('list',{name:'Latest detections by observation time'});
 await list.getByRole('button',{name:'Show Quiet whale on map',exact:true}).waitFor();
 assert.equal(await list.getByRole('button').count(),40);
 assert.equal(await list.getByRole('button',{name:'Show Quiet report on map',exact:true}).count(),2);
 assert.match(await page.locator('.globe').getAttribute('aria-label'),/station-169/);
 await list.getByRole('button',{name:'Show Quiet report on map',exact:true}).last().click();
 await page.getByText('Reported observation location; reported accuracy ±250 m',{exact:true}).waitFor();
 await page.getByRole('button',{name:'Locate observation on map',exact:true}).click();
 await page.waitForFunction(()=>document.querySelector('.loupe .map2d__reported'));
 assert.equal(await page.locator('.loupe .map2d__reported').count(),1);
 await page.getByRole('radio',{name:'2D map',exact:true}).click();
 const world=page.getByRole('group',{name:'World map',exact:true});
 assert.equal(await world.locator('.map2d__marker').count(),20);
 assert.equal(await world.locator('.map2d__reported').count(),2);
 assert.equal(await world.locator('.is-latest').count(),1);
 assert.equal(await world.locator('.is-recent').count(),7);
 await page.getByRole('button',{name:'Map details and coverage'}).click();
 await page.getByText('At connection, 2000 current records were omitted from the initial baseline. Later updates may include some of them.',{exact:true}).waitFor();
 await page.getByRole('checkbox',{name:'Acoustic detections',exact:true}).uncheck();
 assert.equal(await world.locator('.map2d__marker').count(),3);
 await page.getByRole('checkbox',{name:'Reported observations',exact:true}).uncheck();
 assert.equal(await world.locator('.map2d__marker').count(),1);
 // Locating an inspected hidden source restores its type explicitly.
 await page.getByRole('button',{name:'Map details and coverage'}).click();
 await page.getByRole('button',{name:'Locate observation on map',exact:true}).click();
 assert.equal(await world.locator('.map2d__reported').count(),2);
 for(const size of [{width:1280,height:800},{width:1440,height:900}]){await page.setViewportSize(size);assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false)}
 assert.deepEqual(errors,[]);assert.deepEqual(external,[]);
 console.log(JSON.stringify({checks:['40-row diverse feed keeps quiet tracking and observations','exact eight newest map rings','20-symbol diverse map keeps quiet sources','reported triangle in globe/NZ loupe/2D','reported accuracy and unknown identity inspection','explicit locate restores filtered source type','desktop layouts'],errors,external}));
}finally{await browser.close()}
