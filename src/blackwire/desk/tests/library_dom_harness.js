// Real embedded-page events with bounded in-memory responses; no console calls.
const assert=require('assert');
const {createPage,nativeState,catalog,clone,settle}=require('./setup_dom_harness');
const descendants=element=>[element,...element.children.flatMap(descendants)];
function fixture() {
  const value=catalog();
  value.items=Array.from({length:15},(_,n)=>{
    const titleId='PPSA9'+String(n).padStart(4,'0');
    const id='pc:'+titleId+':'+(n+1).toString(16).padStart(64,'0');
    return {id,title:'Library game '+String(n).padStart(2,'0'),titleId,source:'Backups/Game '+n,kind:1,
      startupReady:n===0,canPlay:n===0,canSend:n!==0,playReason:n===0?'':'Prepare startup files first.',sendReason:'',cover:'/api/dashboard/cover?id='+id};
  });
  return value;
}
const covers=app=>app.get('games').children.filter(node=>!node.hidden).map(node=>node.children[0]);
const details=app=>app.get('detailsBody').children.find(node=>node.className==='game-details'&&!node.hidden);
const inBindings=node=>{for(let n=node;n;n=n.parentElement)if(n.className==='detail-bindings')return true;return false;};
const actions=app=>descendants(details(app)).filter(node=>!inBindings(node)&&node.tagName==='BUTTON'&&!node.disabled&&node.getAttribute('aria-disabled')!=='true'&&!node.parentElement.hidden);
async function check() {
  const c=fixture(),app=createPage(Array.from({length:5},nativeState),'5918','',Array.from({length:5},()=>clone(c)));
  await settle();
  assert(app.get('selectedGame').hidden,'Details should open only after the Details control or D shortcut');
  assert.strictEqual(app.get('games').getAttribute('data-layout'),'carousel');
  assert.strictEqual(app.get('layoutCarousel').getAttribute('aria-pressed'),'true');
  assert.strictEqual(app.get('heroTitle').textContent,'Library game 00');
  assert.strictEqual(app.postCount(),0);
  const first=covers(app)[0];first.focus();
  assert(app.key('d').prevented);assert(!app.get('selectedGame').hidden);
  assert(app.get('librarySurface').inert);assert.strictEqual(app.focused(),app.get('closeDetails'));
  assert(app.key('Tab').prevented);assert.strictEqual(app.focused(),app.get('closeDetails'));
  assert(app.key('Tab').prevented);assert.strictEqual(app.focused(),app.get('closeDetails'));
  app.key('Tab',{shiftKey:true});assert.strictEqual(app.focused(),app.get('closeDetails'));
  await app.poll();assert.strictEqual(app.focused(),app.get('closeDetails'));
  assert(app.key('Escape').prevented);assert(app.get('selectedGame').hidden);assert(!app.get('librarySurface').inert);assert.strictEqual(app.focused(),first);
  app.get('layoutGrid').click();assert.strictEqual(app.get('games').getAttribute('data-layout'),'grid');
  assert.strictEqual(app.preferences.get('blackwire-library-layout'),'grid');
  first.focus();app.key('ArrowDown');assert.strictEqual(app.focused(),covers(app)[6]);
  app.key('ArrowDown');assert.strictEqual(app.focused(),covers(app)[12]);
  app.key('End');assert.strictEqual(app.focused(),covers(app)[14]);
  app.key('ArrowDown');assert.strictEqual(app.focused(),covers(app)[14]);
  app.key('d');assert(!app.get('selectedGame').hidden);assert.strictEqual(app.focused(),app.get('closeDetails'));
  assert.strictEqual(actions(app).length,1,'Unready source must not offer Play');
  app.get('closeDetails').click();assert(app.get('selectedGame').hidden);assert.strictEqual(app.focused(),covers(app)[14]);
  app.get('layoutCarousel').click();assert.strictEqual(app.get('heroTitle').textContent,'Library game 14');
  app.get('focusSearch').click();assert.strictEqual(app.focused(),app.get('search'));
  app.get('search').value='no matching title';app.get('search').fire('input');
  assert(app.get('heroCopy').hidden&&app.get('stageArt').hidden&&app.get('selectedGame').hidden);
  app.get('clearSearch').click();assert(!app.get('heroCopy').hidden);
  app.get('openSetup').click();assert(app.get('setupPanel').open);assert.strictEqual(app.focused(),app.get('server'));
  assert.strictEqual(app.postCount(),0,'Browse, layout and details must never submit a game action');
  const restored=createPage([nativeState()],'5918','',[c],{preferences:new Map([['blackwire-library-layout','grid']])});await settle();
  assert.strictEqual(restored.get('games').getAttribute('data-layout'),'grid');assert.strictEqual(restored.postCount(),0);
  const invalid=createPage([nativeState()],'5918','',[c],{preferences:new Map([['blackwire-library-layout','unknown']])});await settle();
  assert.strictEqual(invalid.get('games').getAttribute('data-layout'),'carousel');
  console.log('Library DOM: default carousel, optional retained grid, six-column navigation, selected-source details, Play/Send gating, modal focus/Back, poll focus retention, empty search, header Settings and no implicit action passed.');
}
check().catch(error=>{console.error(error);process.exitCode=1;});
