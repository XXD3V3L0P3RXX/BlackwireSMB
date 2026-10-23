// The actual embedded app with synthetic, in-memory HTTP replies only.
const assert=require('assert');
const {createPage,nativeState,catalog,clone,settle}=require('./setup_dom_harness');
const id='pc:PPSA90001:'+'a'.repeat(64),operation='b'.repeat(32);
function fixture(overrides={}) {
  const c=catalog();
  c.items=[{id,title:'Project game',titleId:'PPSA90001',source:'Project game',kind:1,startupReady:true,
    canPlay:true,canSend:false,playReason:'',sendReason:'',cover:'/api/dashboard/cover?id='+id,...overrides}];
  return c;
}
function page(c,states=null) {return createPage(states || [nativeState(),nativeState(),nativeState()],'5918','',[c,clone(c),clone(c)]);}
function hold(app) {app.replies.push(()=>new Promise(()=>{}));}
const post=app=>app.calls.filter(call=>call.options?.method==='POST');
async function check() {
  // Browsing and rendering the new primary control never starts a game.
  let app=page(fixture());await settle();assert.strictEqual(app.get('heroPrimary').textContent,'Play');
  assert.strictEqual(app.get('heroPrimary').getAttribute('data-action'),'play');assert.strictEqual(app.postCount(),0);
  const cover=app.get('games').children[0].children[0];cover.focus();
  assert(app.key('ArrowUp').prevented);assert.strictEqual(app.focused(),app.get('heroPrimary'));
  assert(app.key('ArrowDown').prevented);assert.strictEqual(app.focused(),cover);
  hold(app);app.get('heroPrimary').click();app.get('heroPrimary').click();await settle();
  assert.strictEqual(app.postCount(),1,'Hero Play must use one existing request');
  let request=post(app)[0];assert.strictEqual(request.url,'/api/dashboard/action');
  assert.strictEqual(request.options.headers['X-Blackwire-Token'],'f'.repeat(32));
  assert.deepStrictEqual(JSON.parse(request.options.body),{id,kind:1,action:'play',catalogGeneration:9});
  assert.strictEqual(app.get('heroPrimary').getAttribute('aria-disabled'),'true');

  // An unprepared source exposes Send, never a second Play path.
  app=page(fixture({startupReady:false,canPlay:false,canSend:true,playReason:'Prepare startup files first.'}));await settle();
  assert.strictEqual(app.get('heroPrimary').textContent,'Send files');assert.strictEqual(app.get('heroPrimary').getAttribute('data-action'),'send');
  hold(app);app.get('heroPrimary').click();app.get('heroPrimary').click();await settle();
  assert.strictEqual(app.postCount(),1);assert.strictEqual(JSON.parse(post(app)[0].options.body).action,'send');

  // Busy, failed or uncertain source checks keep the same server refusal.
  app=page(fixture({canPlay:false,playReason:'The reader update could not be checked.'}));await settle();
  assert(app.get('heroPrimary').disabled);assert.strictEqual(app.get('heroReason').textContent,'The reader update could not be checked.');
  app.get('heroPrimary').click();assert.strictEqual(app.postCount(),0);
  assert(app.get('heroReason').className.includes('blocked'),'A disabled action must expose its reason without clicking its status');
  assert.strictEqual(app.get('heroPrimary').getAttribute('aria-describedby'),'heroReason');
  assert.strictEqual(app.get('heroStatus').getAttribute('aria-expanded'),'true');
  app.get('heroDetails').click();assert(!app.get('selectedGame').hidden);assert(app.get('librarySurface').inert);
  app.get('closeDetails').click();assert(app.get('selectedGame').hidden);

  // The connection chip opens the existing real settings, with saved mask/draft rules.
  app=page(fixture());await settle();app.get('connectionChip').click();
  assert(!app.get('settingsOverlay').hidden);assert(app.get('setupPanel').open);assert(app.get('librarySurface').inert);
  assert.strictEqual(app.focused(),app.get('server'));assert.strictEqual(app.get('server').value,'192.0.2.10');
  assert.strictEqual(app.get('savedPassword').textContent,'********');assert.strictEqual(app.get('smbPassword').value,'');
  app.key('Tab');assert.strictEqual(app.focused(),app.get('shareName'));
  app.key('Escape');assert(app.get('settingsOverlay').hidden);assert(!app.get('librarySurface').inert);
  assert.strictEqual(app.focused(),app.get('connectionChip'));assert.strictEqual(app.postCount(),0);

  // Help is local explanatory content, with a focusable return and no network mutation.
  app.get('openHelp').click();assert(!app.get('helpOverlay').hidden);assert(app.get('librarySurface').inert);
  assert.strictEqual(app.focused(),app.get('closeHelp'));assert(app.key('Tab').prevented);
  app.key('Escape');assert(app.get('helpOverlay').hidden);assert.strictEqual(app.focused(),app.get('openHelp'));
  assert.strictEqual(app.postCount(),0);

  // Both PC Play and adopted PS5 home-screen sessions name the active game.
  for(const pcLaunch of [false,true]) {
    const c=fixture({startupReady:false,canPlay:false,canSend:false,sendReason:'Wait for the current operation to finish.'});
    const liveId='pc:PPSA90002:'+'c'.repeat(64),binding='PPSA90002_'+'c'.repeat(64);
    c.items.push({id:liveId,title:'Project Racer',titleId:'PPSA90002',source:'Racer/PPSA90002.ffpfsc',kind:1,startupReady:true,canPlay:false,canSend:false,playReason:'',sendReason:'',cover:'/api/dashboard/cover?id='+liveId});
    if(pcLaunch)Object.assign(c.operation,{active:true,phase:'running',action:'play',id:'b'.repeat(32),itemId:liveId,title:'Project Racer',message:'Game launched.',
      preparation:{jobId:'',jobState:'',message:'',observed:false,phase:0,files:0,filesTotal:0,bytes:0,bytesTotal:0,ready:false}});
    const running=nativeState();running.stream={phase:'running',game:'Racer/PPSA90002.ffpfsc',sourceBinding:binding,runId:'b'.repeat(32),canClose:true,gamePid:123,readerPid:123};
    app=page(c,[running,clone(running),clone(running)]);await settle();
    app.get('search').value='Project game';app.get('search').fire('input');
    assert.strictEqual(app.get('heroStatusLabel').textContent,'Not ready');
    assert.strictEqual(app.get('heroReason').textContent,'Close Project Racer to send files.');
    assert(!app.get('heroReason').hidden&&app.get('heroReason').className.includes('blocked'));
    assert(app.get('heroPrimary').disabled);app.get('heroPrimary').click();assert.strictEqual(app.postCount(),0);
    app.get('heroStatus').click();assert(app.get('heroReason').className.includes('blocked'),'The required explanation must stay visible');
  }
  app=page(fixture({startupReady:false,canPlay:false,canSend:true}));await settle();
  assert(!app.get('heroPrimary').disabled&&app.get('heroReason').hidden);
  assert(!app.get('heroReason').className.includes('blocked'));assert.strictEqual(app.postCount(),0);

  // Filtering away the selected source cannot leave a stale live Hero action.
  app.get('search').value='No matching game';app.get('search').fire('input');assert(app.get('heroCopy').hidden);
  app.get('heroPrimary').click();assert.strictEqual(app.postCount(),0);
  console.log(JSON.stringify({passed:true,cases:6,actualEmbeddedPage:true,network:'in-memory',consoleContacted:false}));
}
check().catch(error=>{console.error(error);process.exitCode=1;});
