// Actual embedded-page confirmation and Removed flow; modeled HTTP only.
const assert=require('assert');
const {createPage,nativeState,catalog,clone,settle}=require('./setup_dom_harness');
const descendants=element=>[element,...element.children.flatMap(descendants)];
const prepared=()=>{
  const c=catalog(),titleId='PPSA90001',id='pc:'+titleId+':'+ 'a'.repeat(64);
  c.items=[{id,title:'Preserved fixture',titleId,source:'Backups/Preserved fixture',kind:1,startupReady:true,canPlay:true,canSend:false,
    canUninstall:true,removed:false,canRestore:false,uninstallReason:'',restoreReason:'',playReason:'',sendReason:'',cover:'/api/dashboard/cover?id='+id}];return c;
};
const current=app=>app.get('detailsBody').children.find(n=>n.className==='game-details'&&!n.hidden);
const button=(app,label)=>descendants(current(app)).find(n=>n.tagName==='BUTTON'&&n.textContent===(label==='Uninstall'?'Uninstall from PS5':label));
function open(app){app.get('games').children.find(n=>!n.hidden).children[0].click();app.get('heroDetails').click();}
function operation(c,phase='accepted',active=true){
  const item=c.items[0];c.operation={id:'b'.repeat(32),itemId:item.id,title:item.title,action:'uninstall',phase,active,message:'Removing the selected console files.',exitRequested:false,
    preparation:{jobId:'',jobState:'',message:'',observed:false,phase:0,files:0,filesTotal:0,bytes:0,bytesTotal:0,ready:false}};return c;
}
async function check(){
  const c=prepared(),changed=clone(c);changed.catalogGeneration++;
  const app=createPage([nativeState(),nativeState()],'5918','',[c,changed]);await settle();open(app);
  button(app,'Uninstall').click();assert(!app.get('removalConfirm').hidden&&app.get('detailsBody').inert);
  assert.strictEqual(app.focused(),app.get('cancelRemoval'));assert(app.get('removalTitle').textContent.includes('Preserved fixture'));
  assert(app.get('removalDescription').textContent.includes('PC backup and saved games remain'));
  assert(app.key('Tab').prevented);assert.strictEqual(app.focused(),app.get('confirmRemoval'));
  app.key('Escape');assert(app.get('removalConfirm').hidden&&!app.get('detailsBody').inert);assert.strictEqual(app.postCount(),0);
  button(app,'Uninstall').click();await app.poll();assert(app.get('removalConfirm').hidden);app.get('confirmRemoval').click();assert.strictEqual(app.postCount(),0,'Changed catalog must cancel the confirmed source');

  const running=operation(clone(c)),removed=clone(c);removed.catalogGeneration++;
  running.operation.removal={observed:true,phase:2,files:6,filesTotal:12};
  Object.assign(removed.items[0],{startupReady:false,canPlay:false,canSend:false,canUninstall:false,removed:true,canRestore:true});
  removed.operation=clone(running.operation);removed.operation.active=false;removed.operation.phase='stopped';removed.operation.message='The console copy and library entry were removed.';
  const result=createPage(Array.from({length:7},nativeState),'5918','',[c,running,removed,clone(removed)]);await settle();open(result);
  result.replies.push({status:202,body:{ok:true,accepted:true,id:'b'.repeat(32),phase:'accepted',message:'Removal accepted',action:'uninstall',exitRequested:false}});
  button(result,'Uninstall').click();result.get('confirmRemoval').click();result.get('confirmRemoval').click();await settle();
  assert.strictEqual(result.postCount(),1);const request=result.calls.find(call=>call.url==='/api/dashboard/action');
  assert.deepStrictEqual(JSON.parse(request.options.body),{id:c.items[0].id,kind:1,action:'uninstall',catalogGeneration:c.catalogGeneration,confirmedTitleId:'PPSA90001'});
  assert(result.get('sessionMessage').textContent.includes('6 / 12 files'),'Own operation watch must retain measured removal progress');
  assert(result.get('selectedGame').hidden);await result.poll();
  assert(result.get('games').children.every(n=>n.hidden),'Removed source must leave normal library');
  assert(result.get('filter-removed').textContent.includes('(1)'));result.get('filter-removed').click();open(result);
  assert.strictEqual(button(result,'Restore to library').disabled,false);
  assert(button(result,'Play').parentElement.hidden&&button(result,'Send files').parentElement.hidden);
  result.replies.push({status:202,body:{ok:true,accepted:true,id:'c'.repeat(32),phase:'accepted',message:'Restore accepted',action:'restore',exitRequested:false}});
  button(result,'Restore to library').click();await settle();assert.strictEqual(result.postCount(),2);
  const restore=JSON.parse(result.calls.filter(call=>call.url==='/api/dashboard/action')[1].options.body);
  assert.strictEqual(restore.action,'restore');assert(!Object.hasOwn(restore,'confirmedTitleId'));assert.strictEqual(restore.id,c.items[0].id);

  const lost=createPage(Array.from({length:4},nativeState),'5918','',[c,clone(c),clone(c)]);await settle();open(lost);
  lost.replies.push(new Error('Lost confirmation response'));button(lost,'Uninstall').click();lost.get('confirmRemoval').click();await settle();
  await lost.poll();open(lost);button(lost,'Uninstall').click();assert(lost.get('removalConfirm').hidden);assert.strictEqual(lost.postCount(),1,'Lost reply must not issue another request');
  console.log('Removal DOM: named default-Cancel confirmation, focus/Back, changed-generation refusal, one exact confirmed POST, durable Removed presentation, Restore without Play/Send and lost-reply no-retry passed. No console actions.');
}
check().catch(error=>{console.error(error);process.exitCode=1;});
