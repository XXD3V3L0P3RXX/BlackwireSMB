// Exercise the actual page's rescan and PS5 Back controls with in-memory replies.
const assert=require('assert');
const {createPage,nativeState,catalog,clone,settle}=require('./setup_dom_harness');
const id='pc:PPSA90001:'+'a'.repeat(64);
function fixture() {
  const c=catalog();
  c.items=[{id,title:'Project game',titleId:'PPSA90001',source:'Project game',kind:1,startupReady:false,
    canPlay:false,canSend:true,playReason:'Send files first.',sendReason:'',cover:'/api/dashboard/cover?id='+id}];
  return c;
}
const ps5={hostname:'192.0.2.20',userAgent:'Mozilla/5.0 (PlayStation 5) AppleWebKit/605.1.15'};
const posts=app=>app.calls.filter(call=>call.options?.method==='POST');
async function check() {
  const ready=nativeState(),scanning=clone(ready),updated=clone(ready);
  scanning.library.phase='scanning';scanning.library.stale=true;updated.library.generation++;
  const old=fixture(),empty=catalog();empty.catalogGeneration++;
  let app=createPage([ready,scanning,updated],'5918','',[old,clone(old),empty]);await settle();
  assert(!app.get('keyboardHint').hidden&&app.get('consoleHint').hidden);
  assert(app.get('closeConsoleBrowser').hidden);
  assert.strictEqual(app.get('games').children.length,1);
  app.replies.push({status:202,body:{ok:true,message:'Reading the PC library'}});
  app.get('refreshLibrary').click();app.get('refreshLibrary').click();await settle();
  assert.strictEqual(app.postCount(),1,'A refresh click must rescan exactly once, not just poll cached state');
  const request=posts(app)[0];assert.strictEqual(request.url,'/api/refresh');
  assert.strictEqual(request.options.headers['X-Blackwire-Token'],'f'.repeat(32));
  assert.deepStrictEqual(JSON.parse(request.options.body),{});
  assert(app.get('refreshLibrary').disabled);assert.strictEqual(app.get('refreshLibrary').textContent,'Refreshing…');
  assert(!app.get('libraryRefreshStatus').hidden);
  await app.poll();assert.strictEqual(app.get('games').children.length,0,'Moved sources disappear after rescan');
  assert(app.get('heroActions').hidden);assert.strictEqual(app.get('libraryRefreshStatus').textContent,'Library updated.');
  assert(!app.get('refreshLibrary').disabled);assert.strictEqual(app.postCount(),1);
  assert.strictEqual(app.closeCount(),0,'Desktop refresh must never close the browser');

  // Refused or uncertain refreshes stay visible and are never automatically replayed.
  app=createPage([nativeState(),nativeState()],'5918','',[fixture(),fixture()]);await settle();
  app.replies.push({status:409,body:{error:'Library discovery is already running'}});
  app.get('refreshLibrary').click();await settle();
  assert.strictEqual(app.get('libraryRefreshStatus').textContent,'Library discovery is already running');
  assert.strictEqual(app.postCount(),1);
  const running=nativeState();running.stream={phase:'running',game:'Project game',sourceBinding:'PPSA90001_'+'a'.repeat(64),runId:'b'.repeat(32),gamePid:123,canClose:true};
  app=createPage([running],'5918','',[fixture()]);await settle();
  assert(app.get('refreshLibrary').disabled);assert.strictEqual(app.get('libraryRefreshStatus').textContent,'Close Project game to refresh the library.');
  app.get('refreshLibrary').click();assert.strictEqual(app.postCount(),0);

  // Never add a root history entry or cancel native Circle at the library.
  // A native-opened window may refuse script close when history has >1 entries.
  const clock={now:1000};const consoleOptions={...ps5,clock,closeRequiresSingleHistory:true};
  app=createPage([nativeState(),nativeState()],'5918','',[fixture(),fixture()],consoleOptions);await settle();
  assert(app.get('keyboardHint').hidden&&!app.get('consoleHint').hidden);
  assert.strictEqual(app.historyLength(),1);await app.initialPop();await app.poll();assert.strictEqual(app.closeCount(),0);
  clock.now=3000;app.get('heroDetails').click();assert(!app.get('selectedGame').hidden);
  assert(app.key('Escape').prevented);assert(app.get('selectedGame').hidden);assert.strictEqual(app.closeCount(),0);
  app.get('openSetup').click();app.get('shareName').value='Draft share';app.get('shareName').fire('input');
  assert(app.key('Escape').prevented);assert(app.get('settingsOverlay').hidden);assert.strictEqual(app.get('shareName').value,'Draft share');
  app.get('openHelp').click();assert(app.key('Escape').prevented);assert(app.get('helpOverlay').hidden);
  assert.strictEqual(app.historyLength(),1,'Panels must not add synthetic root entries');
  assert(!app.key('Escape').prevented,'Native Circle must remain uncancelled at the library');
  assert.strictEqual(app.closeCount(),1);assert(app.windowClosed());assert.strictEqual(app.postCount(),0);

  // Native Back can omit state; the initial WebKit event must not close a page.
  clock.now=1000;app=createPage([nativeState()],'5918','',[fixture()],consoleOptions);await settle();
  clock.now=3000;app.get('heroDetails').click();await app.initialPop();assert(app.get('selectedGame').hidden);
  await app.initialPop();assert.strictEqual(app.closeCount(),1);assert(app.windowClosed());
  const pad={mapping:'standard',connected:true,buttons:Array.from({length:16},()=>({pressed:false})),axes:[0,0]};
  clock.now=1000;app=createPage([nativeState()],'5918','',[fixture()],{...consoleOptions,pad});await settle();clock.now=3000;
  app.get('games').children[0].children[0].focus();pad.buttons[2].pressed=true;await app.runTimers(80);
  assert(!app.get('selectedGame').hidden,'Square opens Details without a server action');
  pad.buttons[2].pressed=false;await app.runTimers(80);pad.buttons[1].pressed=true;await app.runTimers(80);
  assert(app.get('selectedGame').hidden,'Direct Circle works without a native history event');
  await app.initialPop();assert.strictEqual(app.closeCount(),0,'One press must not dismiss Details and also exit');
  app.get('games').children[0].children[0].focus();app.key('ArrowDown');
  assert.strictEqual(app.focused(),app.get('filter-all'));
  app.key('ArrowRight');app.key('ArrowRight');app.key('ArrowRight');
  assert.strictEqual(app.focused(),app.get('refreshLibrary'),'Refresh is reachable from the covers');
  app=createPage([nativeState()],'5918','',[fixture()],consoleOptions);await settle();
  app.get('heroDetails').click();assert(app.key(undefined,{keyCode:27}).prevented);assert(app.get('selectedGame').hidden);
  assert(!app.key('BrowserBack').prevented);assert.strictEqual(app.closeCount(),1);assert(app.windowClosed());
  app=createPage([nativeState()],'5918','',[fixture()],consoleOptions);await settle();
  assert(!app.get('closeConsoleBrowser').hidden);app.get('closeConsoleBrowser').click();assert.strictEqual(app.closeCount(),1);assert(app.windowClosed());assert.strictEqual(app.postCount(),0);
  app=createPage([nativeState()],'5918','',[fixture()],{hostname:'127.0.0.1',closeThrows:true});await settle();
  assert(app.get('keyboardHint').hidden&&!app.get('consoleHint').hidden);
  assert(!app.key('Escape').prevented,'Even a refused close() must not block the native exit');
  assert.strictEqual(app.postCount(),0);
  console.log(JSON.stringify({passed:true,cases:6,actualEmbeddedPage:true,consoleContacted:false}));
}
check().catch(error=>{console.error(error);process.exitCode=1;});
