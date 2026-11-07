// Injected synthetic host: no owner app, library, audio, or provider requests.
import assert from 'node:assert/strict';
const {chromium}=await import(process.env.PLAYWRIGHT_MODULE||'playwright');
const browser=await chromium.launch({headless:true,channel:'chrome',args:['--enable-unsafe-swiftshader']});
const page=await browser.newPage({viewport:{width:1440,height:900}}), errors=[];
page.on('pageerror',e=>errors.push(e.message));
await page.addInitScript(()=>{
 const piece={id:'managed-piece',name:'Synthetic interval',state:'frozen',origin:'archive_snapshot',eventCount:3,captureTruncated:false};
 if(new URL(location.href).searchParams.has('handoff'))window.atlasPendingReplayPiece=piece;
 const f=window.pieceFixture={calls:[],job:null,empty:false,active:true};
 const methods=['getCapabilities','getCatalog','getLiveSources','getPieceJob','startPieceFreeze','startPieceImport','cancelPieceJob','openManagedPiece','prepareReplay','listSessionsPage'];
 window.atlasHost={request:async r=>{
  f.calls.push({method:r.method,args:r.args});let result;
  if(r.method==='getCapabilities')result={protocolVersion:1,implementation:'native',supportedMethods:methods,libraryAvailable:true,audioAvailable:false,maxEventBatch:500,maxVisibleMarkers:500};
  else if(r.method==='getCatalog')result={limited:true,species:[{id:'tui',commonName:'Tui',scientificName:'',group:'bird'}],sources:[{id:'birdweather',name:'BirdWeather'}],stations:[]};
  else if(r.method==='getLiveSources')result={collecting:false,libraryActive:f.active,sources:['birdweather','nzsharks'].map(id=>({id,enabled:false,polling:false,complete:false,gap:false,lastAttemptUtc:null,lastSuccessUtc:null,lastObservationUtc:null,error:null,records:'0',nextPollSeconds:0}))};
  else if(r.method==='listSessionsPage')result=await new Promise(resolve=>f.releasePage=()=>resolve({items:[],nextCursor:null,sourceId:'c'.repeat(32),revision:'1'}));
  else if(r.method==='getPieceJob')result=f.job;
  else if(r.method==='startPieceFreeze'){
   if(f.empty)return {requestId:r.requestId,ok:false,error:{code:'invalid_request',message:'No collected records in this interval.'}};
   result=f.job={id:'a'.repeat(32),operation:'freeze',status:'running',stage:'writing',processed:1,total:3,piece:null,error:null};
  }else if(r.method==='startPieceImport')result=f.job={id:'b'.repeat(32),operation:'import',status:'completed',processed:3,total:3,piece,error:null};
  else if(r.method==='cancelPieceJob')result=f.job={...f.job,status:'cancelled'};
  else if(r.method==='openManagedPiece')result={session:piece,libraryChanged:false};
  else if(r.method==='prepareReplay')result={preparationId:'prep',generation:'1',durationUs:'1000000',eventCount:3,peakEventsPerSecond:3,session:piece};
  else throw Error(r.method);
  return {requestId:r.requestId,ok:true,result:structuredClone(result)};
 }};
});
try{
 await page.goto(process.env.ATLAS_TEST_URL||'http://127.0.0.1:5186');
 await page.getByLabel('Project',{exact:true}).click();await page.getByText('Freeze collected interval',{exact:true}).click();
 await page.getByRole('button',{name:'Freeze interval',exact:true}).click();await page.getByRole('alert').filter({hasText:'Enter a piece name'}).waitFor();
 await page.getByLabel('Piece name',{exact:true}).fill('Synthetic interval');
 await page.getByText('Missing source or species',{exact:true}).click();
 await page.getByLabel('Exact source ID',{exact:true}).fill('missing-provider');
 assert.equal(await page.getByLabel('Source',{exact:true}).inputValue(),'missing-provider');
 await page.getByLabel('Source',{exact:true}).selectOption('');
 await page.getByLabel('Source',{exact:true}).selectOption('birdweather');await page.getByLabel('Species',{exact:true}).selectOption('tui');
 await page.getByRole('button',{name:'Freeze interval',exact:true}).click();await page.getByRole('progressbar',{name:'Saved piece progress'}).waitFor();
 const args=await page.evaluate(()=>pieceFixture.calls.find(c=>c.method==='startPieceFreeze').args);
 assert.equal(args.sourceId,'birdweather');assert.equal(args.speciesId,'tui');assert.match(args.requestId,/^[0-9a-f]{32}$/);assert.equal(Date.parse(args.endUtc)-Date.parse(args.startUtc),3600000);
 await page.getByLabel('Project',{exact:true}).click();await page.getByText('Saving piece',{exact:true}).waitFor();await page.getByLabel('Project',{exact:true}).click();
 assert.equal(await page.getByLabel('Piece name',{exact:true}).inputValue(),'Synthetic interval');
 await page.getByRole('button',{name:'Cancel piece job'}).click();await page.getByText('Freeze: cancelled',{exact:false}).waitFor();
 await page.evaluate(()=>pieceFixture.empty=true);await page.getByRole('button',{name:'Freeze interval',exact:true}).click();await page.getByRole('alert').filter({hasText:'No collected records'}).waitFor();
 await page.evaluate(()=>pieceFixture.empty=false);await page.getByRole('button',{name:'Freeze interval',exact:true}).click();
 await page.evaluate(()=>pieceFixture.job={...pieceFixture.job,status:'completed',processed:3,piece:{id:'managed-piece',name:'Synthetic interval',state:'frozen',origin:'archive_snapshot',eventCount:3,captureTruncated:false}});
 await page.getByRole('button',{name:'Open saved piece',exact:true}).waitFor();assert.equal(await page.evaluate(()=>pieceFixture.calls.some(c=>c.method==='prepareReplay')),false);
 const previousRequestId=await page.evaluate(()=>pieceFixture.calls.filter(c=>c.method==='startPieceFreeze').at(-1).args.requestId);
 await page.getByRole('button',{name:'Freeze interval',exact:true}).click();
 assert.notEqual(await page.evaluate(()=>pieceFixture.calls.filter(c=>c.method==='startPieceFreeze').at(-1).args.requestId),previousRequestId);
 await page.evaluate(()=>pieceFixture.job={...pieceFixture.job,status:'completed',processed:3,piece:{id:'managed-piece',name:'Synthetic interval',state:'frozen',origin:'archive_snapshot',eventCount:3,captureTruncated:false}});
 await page.getByRole('button',{name:'Open saved piece',exact:true}).click();await page.waitForFunction(()=>pieceFixture.calls.some(c=>c.method==='prepareReplay'&&c.args.sessionId==='managed-piece'));
 await page.waitForFunction(()=>typeof pieceFixture.releasePage==='function');
 await page.evaluate(()=>pieceFixture.releasePage());
 await page.getByText('outside this page.',{exact:false}).waitFor();
 assert.equal(await page.evaluate(()=>pieceFixture.calls.filter(c=>c.method==='prepareReplay').length),1);
 await page.evaluate(()=>pieceFixture.active=false);
 await page.getByRole('button',{name:'Import selected piece to managed library',exact:true}).click();
 await page.getByText('Import: completed',{exact:false}).waitFor();
 assert.equal(await page.evaluate(()=>pieceFixture.calls.find(c=>c.method==='startPieceImport').args.sessionId),'managed-piece');
 await page.goto((process.env.ATLAS_TEST_URL||'http://127.0.0.1:5186')+'?handoff=1');
 await page.waitForFunction(()=>pieceFixture.calls.some(c=>c.method==='prepareReplay'&&c.args.sessionId==='managed-piece'));
 assert.equal(await page.evaluate(()=>window.atlasPendingReplayPiece),undefined);
 assert.deepEqual(errors,[]);console.log(JSON.stringify({checks:['fresh request for intentional repeated freeze','late browse page preserves Replay selection','UTC defaults/validation','source/species IDs, limited-catalogue exact entry and idempotency key','closed Project draft and badge','progress/cancel','empty error','completion and explicit Replay open','selected external piece import','replacement WebView native handoff recovers through Replay'],errors}));
}finally{await browser.close()}
