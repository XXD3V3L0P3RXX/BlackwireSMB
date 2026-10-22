// Exercise the actual embedded page's event handlers and polling, without a console.
const assert=require('assert');
const {createPage,nativeState,catalog,clone,settle}=require('./setup_dom_harness');
const operationId='a'.repeat(32),jobId='b'.repeat(32);
const pcId='pc:PPSA90001:'+'c'.repeat(64),otherId='pc:PPSA90002:'+'d'.repeat(64);
function fixture(stage='idle') {
  const result=catalog();
  result.items=[
    {id:pcId,title:'Preserved Game',titleId:'PPSA90001',source:'Library/Preserved Game',kind:1,canPlay:false,canSend:true,startupReady:false,playReason:'Send startup files first.',sendReason:'',cover:'/api/dashboard/cover?id='+pcId},
    {id:otherId,title:'Another Game',titleId:'PPSA90002',source:'Library/Another Game',kind:1,canPlay:false,canSend:true,startupReady:false,playReason:'Send startup files first.',sendReason:'',cover:'/api/dashboard/cover?id='+otherId}
  ];
  result.operation.action='';result.operation.title='';result.operation.exitRequested=false;
  result.operation.preparation={jobId:'',jobState:'',message:'',observed:false,phase:0,files:0,filesTotal:0,bytes:0,bytesTotal:0,ready:false};
  if(stage==='idle')return result;
  result.operation={id:operationId,itemId:pcId,action:'send',title:'Preserved Game',phase:'running',message:'Preparing startup files',active:true,exitRequested:false,
    preparation:{jobId,jobState:'running',message:'Copying startup files',observed:true,phase:7,files:2,filesTotal:4,bytes:1048576,bytesTotal:2097152,ready:false}};
  if(stage==='verify')Object.assign(result.operation.preparation,{phase:10,files:4,bytes:2097152});
  if(stage==='done'||stage==='error') {
    result.operation.active=false;result.operation.phase='stopped';
    Object.assign(result.operation.preparation,stage==='done'
      ? {phase:12,jobState:'done',files:4,bytes:2097152,ready:true}
      : {phase:13,jobState:'error',message:'The PC share disconnected.'});
  }
  if(stage==='done')Object.assign(result.items[0],{startupReady:true,canPlay:true,canSend:false,playReason:''});
  return result;
}
function rawState() {
  const value=nativeState();value.games=fixture().items.map(item=>({name:item.source,source:item.source,title:item.title,titleId:item.titleId,sourceKind:'folder'}));return value;
}
function descendants(element) {return [element,...element.children.flatMap(descendants)];}
function content(element) {return descendants(element).map(node=>node.textContent).join(' ');}
function details(app,title) {return app.get('detailsBody').children.find(node=>node.className==='game-details'&&descendants(node).some(child=>child.tagName==='H3'&&child.textContent===title));}
function button(app,title,label) {const found=descendants(details(app,title)).find(node=>node.tagName==='BUTTON'&&node.textContent===label);assert(found,'Missing '+label+' for '+title);return found;}
function choose(app,title) {
  const cover=descendants(app.get('games')).find(node=>node.tagName==='BUTTON'&&node.getAttribute('aria-label')==='Details for '+title);
  assert(cover);cover.click();return details(app,title);
}
const ack=()=>({status:202,body:{ok:true,accepted:true,id:operationId,phase:'accepted',message:'Preparing startup files',action:'send',exitRequested:false}});
function networkReply(value) {return {ok:value.status>=200&&value.status<300,status:value.status,json:()=>Promise.resolve(clone(value.body))};}
async function check() {
  const catalogs=[fixture(),fixture('copy'),fixture('verify'),fixture('done'),fixture('done')];
  const app=createPage(Array.from({length:catalogs.length},rawState),'5918','',catalogs);await settle();
  choose(app,'Preserved Game');const send=app.get('heroPrimary');send.focus();
  assert.strictEqual(app.postCount(),0);assert(app.get('preparationStatus').hidden);
  let finishRequest;app.replies.push(()=>new Promise(resolve=>{finishRequest=resolve;}));
  send.click();send.click();
  assert.strictEqual(app.postCount(),1);assert(!app.get('preparationStatus').hidden);
  assert.strictEqual(app.get('preparationTitle').textContent,'Preserved Game');
  assert.strictEqual(app.get('preparationStatus').getAttribute('data-state'),'requesting');
  assert.strictEqual(app.get('preparationTrack').getAttribute('aria-valuenow'),null);
  assert.strictEqual(app.focused(),send);
  finishRequest(networkReply(ack()));await settle();
  assert.strictEqual(app.postCount(),1);assert.strictEqual(app.focused(),send);
  assert.strictEqual(app.get('preparationTrack').getAttribute('aria-valuenow'),'50');
  assert(app.get('preparationDetail').textContent.includes('2 / 4 files'));
  assert(app.get('preparationDetail').textContent.includes('1.0 / 2.0 MiB'));
  assert.strictEqual(button(app,'Preserved Game','Preparing…').getAttribute('aria-disabled'),'true');
  assert(content(details(app,'Another Game')).includes('Preparing Preserved Game'));
  // The app keeps rendering and browsing while the first job is active.
  choose(app,'Another Game');const otherSend=app.get('heroPrimary');otherSend.click();
  assert.strictEqual(app.postCount(),1);assert.strictEqual(otherSend.getAttribute('aria-disabled'),'true');assert(app.get('preparationStatus').hidden,'Other-source progress must not become a global notification');
  app.get('search').focus();await app.poll();
  assert.strictEqual(app.focused(),app.get('search'));assert(app.get('preparationDetail').textContent.includes('Verifying the game index'));
  assert.strictEqual(app.get('preparationStatus').getAttribute('data-state'),'preparing');
  assert(app.get('preparationToast').hidden);assert.strictEqual(app.postCount(),1);
  // Completion is attached to this exact job and fresh source readiness.
  await app.poll();assert.strictEqual(app.get('preparationStatus').getAttribute('data-state'),'ready');
  assert.strictEqual(app.get('preparationToastTitle').textContent,'Preserved Game is ready');
  assert(app.get('preparationToast').hidden);assert.strictEqual(app.focused(),app.get('search'));
  const readyDetails=details(app,'Preserved Game'),play=button(app,'Preserved Game','Play');
  assert(!play.disabled);assert(!play.parentElement.hidden);
  assert(button(app,'Preserved Game','Send files').parentElement.hidden);
  app.get('filter-not-ready').click();app.get('search').value='Another';app.get('search').fire('input');
  assert(readyDetails.hidden);app.get('preparationInspect').click();
  assert(!readyDetails.hidden);assert.strictEqual(app.get('search').value,'');assert.strictEqual(app.focused(),app.get('closeDetails'));
  await app.poll();assert.strictEqual(app.postCount(),1);assert(!app.calls.some(call=>call.url==='/api/play'||call.url==='/api/start'));
  const request=JSON.parse(app.calls.find(call=>call.url==='/api/dashboard/action').options.body);
  assert.strictEqual(request.id,pcId);assert.strictEqual(request.action,'send');
  assert.strictEqual(app.calls.find(call=>call.url==='/api/dashboard/action').options.headers['X-Blackwire-Token'],'f'.repeat(32));

  // A completed banner expires once; status polls must not resurrect it or remove Play/readiness.
  const expiry=createPage([rawState(),rawState(),rawState(),rawState()],'5918','',[fixture('copy'),fixture('done'),fixture('done'),fixture('done')]);await settle();
  await expiry.poll();choose(expiry,'Preserved Game');assert(!expiry.get('preparationStatus').hidden);
  expiry.get('search').focus();await expiry.runTimers(5000);
  assert(expiry.get('preparationStatus').hidden);assert(expiry.get('preparationToast').hidden);assert.strictEqual(expiry.focused(),expiry.get('search'));
  await expiry.poll();assert(expiry.get('preparationStatus').hidden);assert(!button(expiry,'Preserved Game','Play').disabled);
  assert.strictEqual(expiry.postCount(),0);assert.strictEqual(expiry.get('sessionElapsed'),undefined);
  const newWork=fixture('copy');newWork.operation.id='e'.repeat(32);newWork.operation.preparation.jobId='f'.repeat(32);
  const replaced=createPage([rawState(),rawState(),rawState()],'5918','',[fixture('copy'),fixture('done'),newWork]);await settle();
  await replaced.poll();await replaced.poll();assert.strictEqual(replaced.get('preparationStatus').getAttribute('data-state'),'preparing');
  choose(replaced,'Preserved Game');await replaced.runTimers(5000);assert(!replaced.get('preparationStatus').hidden);assert.strictEqual(replaced.postCount(),0);

  // A lost response never resends; a fresh matching operation resolves the watch.
  const lostCatalogs=[fixture(),fixture(),fixture('copy')];
  const lost=createPage(Array.from({length:lostCatalogs.length},rawState),'5918','',lostCatalogs);await settle();choose(lost,'Preserved Game');
  lost.replies.push(new Error('Connection interrupted.'));button(lost,'Preserved Game','Send files').click();await settle();
  assert.strictEqual(lost.postCount(),1);assert.strictEqual(lost.get('preparationStatus').getAttribute('data-state'),'unknown');
  assert(lost.get('preparationDetail').textContent.includes('No request was repeated'));
  button(lost,'Preserved Game','Preparing…').click();assert.strictEqual(lost.postCount(),1);
  lost.get('preparationRefresh').focus();lost.get('preparationRefresh').click();await settle();
  assert.strictEqual(lost.focused(),lost.get('preparationRefresh'));assert.strictEqual(lost.get('preparationStatus').getAttribute('data-state'),'preparing');
  assert.strictEqual(lost.get('preparationTrack').getAttribute('aria-valuenow'),'50');assert.strictEqual(lost.postCount(),1);

  // Definite refusal and worker failure are named, persistent, and need an explicit next action.
  const refused=createPage([rawState(),rawState(),rawState()],'5918','',[fixture(),fixture(),fixture()]);await settle();choose(refused,'Preserved Game');
  refused.replies.push({status:409,body:{error:'The source changed. Refresh the library.'}});
  button(refused,'Preserved Game','Send files').click();await settle();
  assert.strictEqual(refused.get('preparationStatus').getAttribute('data-state'),'error');
  assert(refused.get('preparationDetail').textContent.includes('The source changed.'));assert.strictEqual(refused.get('preparationTitle').textContent,'Preserved Game');
  await refused.poll();assert.strictEqual(refused.postCount(),1);assert.strictEqual(refused.get('preparationStatus').getAttribute('data-state'),'error');
  const failed=createPage([rawState(),rawState(),rawState()],'5918','',[fixture('copy'),fixture('error'),fixture('error')]);await settle();
  await failed.poll();assert.strictEqual(failed.get('preparationStatus').getAttribute('data-state'),'error');
  assert.strictEqual(failed.get('preparationToastTitle').textContent,'Preserved Game needs attention');
  assert(failed.get('preparationDetail').textContent.includes('The PC share disconnected.'));
  await failed.poll();assert.strictEqual(failed.postCount(),0);assert.strictEqual(failed.get('preparationTrack').getAttribute('aria-valuenow'),null);
  choose(failed,'Preserved Game');await failed.runTimers(9000);assert(!failed.get('preparationStatus').hidden);
  for(const sample of [app,lost,refused,failed]) {
    assert.strictEqual(sample.get('server').value,'192.0.2.10');assert.strictEqual(sample.get('smbPassword').value,'');
    assert.strictEqual(sample.get('smbPassword').placeholder,'********');
  }
  const unresolved=fixture('done');unresolved.operation.preparation.ready=false;
  Object.assign(unresolved.items[0],{startupReady:false,canPlay:false,canSend:true});
  const waiting=createPage([rawState(),rawState()],'5918','',[unresolved,clone(unresolved)]);await settle();choose(waiting,'Preserved Game');
  assert.strictEqual(waiting.get('preparationStatus').getAttribute('data-state'),'unknown');
  const blockedSend=button(waiting,'Preserved Game','Preparing…');assert(blockedSend.disabled);blockedSend.click();
  waiting.get('preparationRefresh').click();await settle();assert.strictEqual(waiting.postCount(),0);
  assert.strictEqual(waiting.get('preparationStatus').getAttribute('data-state'),'unknown');assert(waiting.get('saveSetup').disabled);
  // A terminal Play failure supersedes old completed Send history. Dismissing
  // its compact notice survives polling, but never hides uncertain active work.
  const launchFailed=fixture('done');
  launchFailed.operation={...fixture().operation,id:'9'.repeat(32),itemId:pcId,action:'play',title:'Preserved Game',phase:'refused',
    message:'registration_ownership_intent_unconfirmed',active:false,exitRequested:false};
  const historical=rawState();historical.jobs=[{id:jobId,name:'Library/Preserved Game',action:'send',state:'done',filesPrepared:true,
    progress:{phase:12,files:4,filesTotal:4,bytes:2097152,bytesTotal:2097152}}];
  historical.stream={phase:'error',runId:'8'.repeat(32),game:'Library/Preserved Game',gamePid:0,readerPid:0,canClose:false,message:launchFailed.operation.message};
  const uncertain=clone(launchFailed);uncertain.operation.phase='unknown';uncertain.operation.active=true;
  const latest=createPage([historical,historical,historical],'5918','',[launchFailed,clone(launchFailed),uncertain]);await settle();
  assert(latest.get('preparationStatus').hidden);assert(latest.get('sessionStatus').hidden);
  assert.strictEqual(latest.get('sessionHeading').textContent,'Launch failed');
  assert(latest.get('sessionMessage').textContent.includes('No game was started'));assert(latest.get('sessionOwnership').hidden);
  assert(!latest.get('sessionDismiss').hidden);latest.get('sessionDismiss').click();assert(latest.get('sessionStatus').hidden);
  await latest.poll();assert(latest.get('sessionStatus').hidden);assert(latest.get('preparationStatus').hidden);
  await latest.poll();assert(latest.get('sessionStatus').hidden);assert(latest.get('sessionDismiss').hidden);
  assert.strictEqual(latest.postCount(),0);
  console.log('Offline preparation DOM: one Send, immediate named acknowledgement, retained focus and browsing, measured file/byte progress, verification before Ready, exact-source completion notice, Play-only Ready actions, visible refusal/failure and lost-response recovery without retry passed.');
}
check().catch(error=>{console.error(error);process.exitCode=1;});
