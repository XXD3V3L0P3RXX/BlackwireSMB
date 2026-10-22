// Actual embedded model/app; modeled window, HTTP and timers. No console access.
const assert=require('assert'),fs=require('fs'),vm=require('vm');
const page=fs.readFileSync(process.argv[2],'utf8');
const script=id=>page.match(new RegExp('<script id="'+id+'">([\\s\\S]*?)</script>'))[1];
const context={module:{exports:{}}};vm.createContext(context);vm.runInContext(script('blackwire-view-model'),context);
const ui=context.module.exports;
const id='pc:PPSA90001:'+'a'.repeat(64),oid='b'.repeat(32);
const body={id,action:'play'};
const ack={id:oid,ok:true,accepted:true,action:'play',phase:'accepted'};
const running={id:oid,itemId:id,action:'play',active:true,phase:'running'};
const fresh=(hostname='127.0.0.1',userAgent='')=>ui.browserHandoff({hostname},{userAgent});
function outcome(h,op,close=false,visible=false){const got=h.update(op);assert.strictEqual(got.attemptClose,close);assert.strictEqual(got.visible,visible);}
// 1: An existing session alone cannot close the browser.
outcome(fresh(),running);
// 2: Exact acknowledged operation, item, action and running phase are required.
for(const delta of [{id:'c'.repeat(32)},{itemId:id.slice(0,-1)+'c'},{action:'send'},{phase:'accepted'},{phase:'unknown'},{phase:'refused'},{phase:'stopped'},{active:false}]) {
 const h=fresh();h.accepted(body,ack);outcome(h,{...running,...delta});
}
// 3: Valid running closes once, duplicate acceptance/polls never replay it.
const once=fresh();once.accepted(body,ack);outcome(once,running,true,true);
for(let n=0;n<5;n++){once.accepted(body,ack);outcome(once,running,false,true);}
outcome(once,{...running,phase:'unknown'},false,true);
outcome(once,{...running,phase:'stopped',active:false});
once.accepted(body,ack);outcome(once,running,false,true);
// 4: Send, refused/lost/malformed acceptance never arms browser handoff.
for(const [b,a] of [[{...body,action:'send'},{...ack,action:'send'}],[body,{...ack,accepted:false}],
 [body,{...ack,ok:false}],[body,{...ack,id:'0'.repeat(32)}],[body,{...ack,phase:'unknown'}],[body,null],
 [{...body,id:'bad'},ack],[body,{...ack,action:'uninstall'}]]) {const h=fresh();h.accepted(b,a);outcome(h,running);}
// 5: Ordinary LAN PC clients keep the page open.
for(const host of ['192.0.2.20','console.example','localhost','127.0.0.1.example']) {
 const h=fresh(host,'Mozilla/5.0 Chrome/130.0');h.accepted(body,ack);outcome(h,running);
}
// 6: Actual PlayStation UA is eligible on LAN; IPv6 loopback also eligible.
for(const [host,ua] of [['192.0.2.20','Mozilla/5.0 (PlayStation; PlayStation 5/7.61) AppleWebKit/605.1.15'],['::1',''],['[::1]','']]) {
 const h=fresh(host,ua);h.accepted(body,ack);outcome(h,running,true,true);
}
// 7: Automatic close attempts stay bounded and old IDs cannot replay.
const bounded=fresh();for(let n=1;n<=130;n++){const opId=n.toString(16).padStart(32,'0');bounded.accepted(body,{...ack,id:opId});outcome(bounded,{...running,id:opId},n<=128,true);}
bounded.accepted(body,{...ack,id:'1'.padStart(32,'0')});outcome(bounded,{...running,id:'1'.padStart(32,'0')},false,true);
const oldRun='c'.repeat(32),newRun='d'.repeat(32),nextRun='e'.repeat(32);
const nativeRun=(runId=newRun,phase='running',gamePid=4321)=>({runId,phase,gamePid,game:'Backups/Preserved fixture'});
function streamOutcome(h,stream,close=false,visible=false,op=null,isFresh=true) {
 const got=h.update(op,stream,isFresh);assert.strictEqual(got.attemptClose,close);assert.strictEqual(got.visible,visible);
}
// 10: Loading, reloading, or inspecting the first existing run never dismisses it.
for(const phase of ['preparing','preflight','launching','running','unknown','stopped','error','unverified']) {
 const h=fresh();streamOutcome(h,nativeRun(oldRun,phase));streamOutcome(h,nativeRun(oldRun));
 streamOutcome(h,nativeRun(oldRun,'stopped',0));streamOutcome(h,nativeRun(oldRun));
}
// 11: A different remotely initiated run requires running plus a real integer game PID.
const remote=fresh();streamOutcome(remote,nativeRun(oldRun));
for(const phase of ['preparing','preflight','launching','closing','unknown','stopped','error','unverified'])streamOutcome(remote,nativeRun(newRun,phase));
for(const pid of [undefined,null,0,1,-1,1.5,'4321',Infinity,NaN,2147483648])streamOutcome(remote,{...nativeRun(),gamePid:pid});
streamOutcome(remote,nativeRun(newRun),true,true);
streamOutcome(remote,nativeRun(newRun),false,true);
streamOutcome(remote,nativeRun(newRun,'unknown'),false,true);
streamOutcome(remote,nativeRun(newRun,'stopped',0));
streamOutcome(remote,nativeRun(newRun),false,true);
streamOutcome(remote,nativeRun(nextRun,'preparing',0));
streamOutcome(remote,nativeRun(nextRun),true,true);
streamOutcome(remote,nativeRun(oldRun),false,true);
// 12: Idle is a baseline; malformed/nonces and stale status cannot arm or consume a run.
const idle=fresh();streamOutcome(idle,{phase:'stopped'});
for(const runId of ['',null,'0'.repeat(32),'bad'])streamOutcome(idle,nativeRun(runId));
streamOutcome(idle,nativeRun(newRun),false,false,null,false);
streamOutcome(idle,nativeRun(newRun),true,true);
const missing=fresh();streamOutcome(missing,{phase:'running'});streamOutcome(missing,nativeRun(oldRun));
streamOutcome(missing,nativeRun(newRun),true,true);
// 13: Native and own-ack paths share one close regardless of which reaches running first.
for(const nativeFirst of [false,true]) {
 const h=fresh();streamOutcome(h,nativeRun(oldRun));h.accepted(body,ack);
 if(nativeFirst) {
  streamOutcome(h,nativeRun(newRun),true,true,null);
  streamOutcome(h,nativeRun(newRun),false,true,running);
 } else {
  streamOutcome(h,nativeRun(oldRun),true,true,running);
  streamOutcome(h,nativeRun(newRun),false,true,running);
 }
 h.accepted(body,ack);streamOutcome(h,nativeRun(newRun),false,true,running);
}
// 19: An ACK arriving after the native close cannot replay it or block a subsequent run.
const delayed=fresh();streamOutcome(delayed,{phase:'stopped'});streamOutcome(delayed,nativeRun(newRun),true,true);
delayed.accepted(body,ack);streamOutcome(delayed,nativeRun(newRun),false,true,running);
streamOutcome(delayed,nativeRun(newRun),false,true,running);
streamOutcome(delayed,nativeRun(nextRun),true,true,running);
streamOutcome(delayed,nativeRun(nextRun),false,true,running);
// 14: The remote observer retains ordinary desktop tabs and bounds all new-run attempts.
for(const host of ['192.0.2.20','console.example','localhost','127.0.0.1.example']) {
 const h=fresh(host,'Mozilla/5.0 Chrome/130.0');streamOutcome(h,{phase:'stopped'});streamOutcome(h,nativeRun());
}
const boundedRemote=fresh();streamOutcome(boundedRemote,{phase:'stopped'});
for(let n=1;n<=130;n++)streamOutcome(boundedRemote,nativeRun(n.toString(16).padStart(32,'0')),n<=128,true);
streamOutcome(boundedRemote,nativeRun('1'.padStart(32,'0')),false,true);
const {createPage,nativeState,catalog,clone,settle}=require('./setup_dom_harness');
const descendants=e=>[e,...e.children.flatMap(descendants)];
function fixture(){const c=catalog();c.items=[{id,title:'Preserved fixture',titleId:'PPSA90001',source:'Backups/Preserved fixture',kind:1,
 startupReady:true,canPlay:true,canSend:false,playReason:'',sendReason:'',cover:'/api/dashboard/cover?id='+id}];return c;}
function op(c,phase='running',active=true){c.operation={...running,phase,active,title:'Preserved fixture',message:'Started',exitRequested:false,
 preparation:{jobId:'',jobState:'',message:'',observed:false,phase:0,files:0,filesTotal:0,bytes:0,bytesTotal:0,ready:false}};return c;}
function play(app){app.get('games').children.find(n=>!n.hidden).children[0].click();const details=app.get('detailsBody').children.find(n=>n.className==='game-details'&&!n.hidden);
 descendants(details).find(n=>n.tagName==='BUTTON'&&n.textContent==='Play').click();}
async function check(){
 // 8: The actual app calls window.close once and retains visible fallback across polling.
 const c=fixture(),run=op(clone(c)),stop=op(clone(c),'stopped',false);
 const app=createPage(Array.from({length:6},nativeState),'5918','',[c,run,clone(run),stop],{hostname:'127.0.0.1'});
 await settle();assert(app.get('browserHandoff').hidden);assert.strictEqual(app.closeCount(),0);
 app.replies.push({status:202,body:{...ack,message:'Accepted',exitRequested:false}});play(app);await settle();
 assert.strictEqual(app.closeCount(),1);assert(!app.get('browserHandoff').hidden);
 assert(page.includes('Game started. Close the PS5 browser with the controller to see it.'));
 assert(page.indexOf('id="browserHandoff"')>page.indexOf('</main>'),'Fallback must be outside the library stacking context');
 assert(/\.browser-handoff\s*\{[^}]*position:fixed;[^}]*z-index:50;/.test(page),'Fallback must remain visible above the details modal');
 await app.poll();assert.strictEqual(app.closeCount(),1);assert(!app.get('browserHandoff').hidden);
 await app.poll();assert.strictEqual(app.closeCount(),1);assert(app.get('browserHandoff').hidden);assert.strictEqual(app.postCount(),1);
 // 9: A browser refusing close leaves the exact fallback and no retry or extra request.
 const blocked=createPage(Array.from({length:5},nativeState),'5918','',[c,run,clone(run)],{hostname:'127.0.0.1',closeThrows:true});
 await settle();blocked.replies.push({status:202,body:{...ack,message:'Accepted',exitRequested:false}});play(blocked);await settle();await blocked.poll();
 assert.strictEqual(blocked.closeCount(),1);assert(!blocked.get('browserHandoff').hidden);assert.strictEqual(blocked.postCount(),1);
 // 15: A PS5 page observes ordinary remote Play through existing state polls, without POSTs.
 const state=stream=>({...nativeState(),stream});
 const receiver=createPage([state(nativeRun(oldRun)),state(nativeRun(newRun,'preparing',0)),state(nativeRun(newRun,'launching',0)),
  state(nativeRun(newRun,'running',0)),state(nativeRun(newRun)),state(nativeRun(newRun)),state(nativeRun(newRun,'stopped',0)),
  state(nativeRun(nextRun))],'5918','',null,{hostname:'192.0.2.20',userAgent:'Mozilla/5.0 (PlayStation; PlayStation 5/7.61)',closeThrows:true});
 await settle();assert.strictEqual(receiver.closeCount(),0);assert(receiver.get('browserHandoff').hidden);
 for(let n=0;n<3;n++){await receiver.poll();assert.strictEqual(receiver.closeCount(),0);assert(receiver.get('browserHandoff').hidden);}
 await receiver.poll();assert.strictEqual(receiver.closeCount(),1);assert(!receiver.get('browserHandoff').hidden);
 await receiver.poll();assert.strictEqual(receiver.closeCount(),1);assert(!receiver.get('browserHandoff').hidden);
 await receiver.poll();assert(receiver.get('browserHandoff').hidden);
 await receiver.poll();assert.strictEqual(receiver.closeCount(),2);assert(!receiver.get('browserHandoff').hidden);
 assert.strictEqual(receiver.postCount(),0);assert(receiver.calls.every(call=>['/api/state','/api/dashboard/library'].includes(call.url)));
 // 16: Desktop observers stay open, including after an actual remote running transition.
 const desktop=createPage([state({phase:'stopped'}),state(nativeRun())]);await settle();await desktop.poll();
 assert.strictEqual(desktop.closeCount(),0);assert(desktop.get('browserHandoff').hidden);assert.strictEqual(desktop.postCount(),0);
 // 17: Failed core polls cannot trigger a close, and failed dashboard polls do not hide fresh native evidence.
 const unavailable=createPage([state({phase:'stopped'}),new Error('Status unavailable'),state(nativeRun())],'5918','',
  [c,new Error('Dashboard temporarily unavailable')],{hostname:'127.0.0.1'});
 await settle();await unavailable.poll();assert.strictEqual(unavailable.closeCount(),0);assert(unavailable.get('browserHandoff').hidden);
 await unavailable.poll();assert.strictEqual(unavailable.closeCount(),1);assert(!unavailable.get('browserHandoff').hidden);assert.strictEqual(unavailable.postCount(),0);
 // 18: The current Send readiness presentation is retained by this isolated UI change.
 const setupGame={name:'source',source:'source',titleId:'PPSA90001',kind:1,canPrepare:true,canPlay:false,startupReady:false,setupOnly:true,prepared:true};
 const setupState={backend:'native',games:[setupGame],stream:{phase:'stopped'},jobs:[]};
 assert.strictEqual(ui.actions(setupGame,setupState,null).prepareLabel,'Finish setup');
 setupState.dashboard={items:[{id,source:'source',titleId:'PPSA90001',kind:1,canSend:true,canPlay:false,startupReady:false}],operation:{phase:'idle'}};
 const converted=ui.libraryGames(setupState)[0];assert.strictEqual(converted.setupOnly,true);assert.strictEqual(converted.prepared,true);
 assert.strictEqual(ui.actions(converted,setupState,null).prepareLabel,'Finish setup');
 console.log(JSON.stringify({passed:true,cases:19,actualEmbeddedPage:true,consoleContacted:false,browserCloseHardwareVerified:false}));
}
check().catch(error=>{console.error(error);process.exitCode=1;});
