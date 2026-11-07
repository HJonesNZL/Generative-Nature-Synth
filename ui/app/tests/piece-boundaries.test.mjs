import test from 'node:test';
import assert from 'node:assert/strict';
import { HostClient } from '../src/host/client.ts';
globalThis.window={setTimeout,clearTimeout};
const piece={id:'piece',name:'Saved interval',state:'frozen',origin:'archive_snapshot',eventCount:3,captureTruncated:false};
const job={id:'a'.repeat(32),operation:'freeze',status:'completed',processed:3,total:3,piece,error:null};
const methods=['startPieceFreeze','startPieceImport','getPieceJob','cancelPieceJob','openManagedPiece'];
async function fixture(){
 let value=job;
 const client=new HostClient({request:async request=>({requestId:request.requestId,ok:true,result:request.method==='getCapabilities'?{protocolVersion:1,implementation:'native',supportedMethods:methods,audioAvailable:false,maxEventBatch:500,maxVisibleMarkers:500}:value})});
 await client.connect();return {client,set:v=>value=v};
}
test('piece jobs enforce bounds, status/result invariants, identities and operation',async()=>{
 const f=await fixture();
 for(const value of [undefined,{},[],{...job,stage:'x'.repeat(65)},{...job,stage:42},{...job,id:'x'},{...job,operation:'other'},{...job,status:'ready'},{...job,processed:-1},{...job,total:2},{...job,total:50001},{...job,processed:3.2},{...job,processed:NaN},{...job,piece:null},{...job,piece:{...piece,eventCount:4}},{...job,piece:{...piece,id:'x'.repeat(129)}},{...job,piece:{...piece,observedBoundsUs:['2','1']}},{...job,error:'Unexpected'},{...job,status:'failed',piece:null,error:null},{...job,status:'running',piece:null,error:'e'.repeat(4097)},{...job,status:'running'}]){
  f.set(value);assert.equal((await f.client.call('getPieceJob',{})).ok,false,JSON.stringify(value));
 }
 for(const value of [null,job,{...job,status:'running',processed:0,total:null,piece:null},{...job,status:'cancelled',piece:null},{...job,status:'failed',piece:null,error:'Failed to verify'}]){
  f.set(value);assert.equal((await f.client.call('getPieceJob',{})).ok,true);
 }
 f.set(job);assert.equal((await f.client.call('getPieceJob',{jobId:'b'.repeat(32)})).ok,false);
 assert.equal((await f.client.call('cancelPieceJob',{jobId:'b'.repeat(32)})).ok,false);
 assert.equal((await f.client.call('startPieceImport',{sessionId:'piece'})).ok,false);
 f.set({...job,operation:'import'});assert.equal((await f.client.call('startPieceFreeze',{})).ok,false);
 assert.equal((await f.client.call('startPieceImport',{sessionId:'wrong'})).ok,false);
 assert.equal((await f.client.call('startPieceImport',{sessionId:'piece'})).ok,true);
 f.set(null);assert.equal((await f.client.call('startPieceFreeze',{})).ok,false);
});
test('managed open validates returned selection, flag and bounded session',async()=>{
 const f=await fixture();
 for(const value of [null,{}, {session:piece,libraryChanged:'false'},{session:{...piece,id:'wrong'},libraryChanged:false},{session:{...piece,eventCount:50001},libraryChanged:true},{session:{...piece,metadataIntegrity:'x'.repeat(129)},libraryChanged:false}]){
  f.set(value);assert.equal((await f.client.call('openManagedPiece',{sessionId:'piece'})).ok,false);
 }
 for(const libraryChanged of [true,false]){f.set({session:piece,libraryChanged});assert.equal((await f.client.call('openManagedPiece',{sessionId:'piece'})).ok,true)}
});
