// Real app events; synthetic source identities and in-memory HTTP only.
const assert=require('assert');
const {createPage,nativeState,catalog,clone,settle}=require('./setup_dom_harness');
const alpha='pc:PPSA90001:'+'a'.repeat(64),beta='pc:PPSA90002:'+'b'.repeat(64);
const descendants=node=>[node,...node.children.flatMap(descendants)];
function fixture(phase='copy') {
  const c=catalog();
  c.items=[['Alpha',alpha],['Beta',beta]].map(([title,id])=>({id,title,titleId:id.split(':')[1],source:'Library/'+title,kind:1,
    startupReady:false,canPlay:false,canSend:true,playReason:'Send startup files first.',sendReason:'',cover:'/api/dashboard/cover?id='+id}));
  c.operation={id:'c'.repeat(32),itemId:alpha,action:'send',title:'Alpha',phase:phase==='unknown'?'unknown':'running',message:'Preparing Alpha',active:true,exitRequested:false,
    preparation:{jobId:'d'.repeat(32),jobState:'running',message:'Copying startup files',observed:true,phase:7,files:2,filesTotal:4,bytes:1048576,bytesTotal:2097152,ready:false}};
  return c;
}
function choose(app,title) {descendants(app.get('games')).find(node=>node.tagName==='BUTTON'&&node.getAttribute('aria-label')==='Details for '+title).click();}
function details(app,title) {return app.get('detailsBody').children.find(node=>node.className==='game-details'&&descendants(node).some(child=>child.tagName==='H3'&&child.textContent===title));}
const progress=(app,title)=>details(app,title).children.find(node=>String(node.className || '').startsWith('progress'));
async function check() {
  const c=fixture(),app=createPage([nativeState(),nativeState(),nativeState()],'5918','',[c,clone(c),clone(c)]);
  await settle();choose(app,'Alpha');
  assert(app.get('selectedGame').hidden,'Preview cover click selects without opening an overlay');
  assert(!app.get('preparationStatus').hidden);assert.strictEqual(app.get('preparationTrack').getAttribute('aria-valuenow'),'50');
  assert.strictEqual(app.get('heroStatusLabel').textContent,'Preparing');assert.strictEqual(app.get('heroPrimary').textContent,'Preparing…');
  assert.strictEqual(app.get('heroPrimary').getAttribute('aria-disabled'),'true');assert.strictEqual(app.postCount(),0);

  app.get('heroDetails').click();assert(!app.get('selectedGame').hidden);
  assert(app.get('preparationStatus').hidden,'No duplicate progress behind the matching Details overlay');
  assert(!progress(app,'Alpha').hidden,'Actual progress remains on the matching game card');
  const detailTrack=descendants(progress(app,'Alpha')).find(node=>node.getAttribute('role')==='progressbar');
  assert.strictEqual(detailTrack.getAttribute('aria-valuenow'),'50');
  assert.strictEqual(app.focused(),app.get('closeDetails'),'Details opens on its safe Back action');
  const reason=details(app,'Alpha').children.find(node=>node.className==='detail-reason');
  assert(reason.hidden,'Do not repeat a generic busy reason beside the same progress');

  app.get('closeDetails').click();choose(app,'Beta');
  assert(app.get('preparationStatus').hidden,'Other games never receive a global preparation notice');
  app.get('heroDetails').click();assert(progress(app,'Alpha').hidden&&progress(app,'Beta').hidden);
  app.get('closeDetails').click();
  const alphaCard=app.get('games').children.find(node=>descendants(node).some(child=>child.getAttribute('aria-label')==='Details for Alpha'));
  const alphaDot=descendants(alphaCard).find(node=>node.className==='badge');
  assert.strictEqual(alphaDot.style.background,'var(--blue)','The preparing game retains only its own compact rail badge');
  app.get('heroPrimary').click();assert.strictEqual(app.postCount(),0,'Another source stays blocked by the original active-job gate');

  app.get('filter-ready').click();assert(app.get('heroCopy').hidden&&app.get('heroActions').hidden);
  assert.strictEqual(app.get('resultCount').textContent,'0 games');assert(app.get('preparationStatus').hidden);
  app.get('filter-all').click();choose(app,'Alpha');assert(!app.get('preparationStatus').hidden);
  assert(app.get('sessionStatus').hidden&&app.get('preparationToast').hidden,'No global session or completion notifications');
  app.get('openSetup').click();assert(app.get('preparationStatus').hidden);
  app.get('closeSetup').click();assert(!app.get('preparationStatus').hidden);
  assert.strictEqual(app.postCount(),0);

  const uncertain=createPage([nativeState()],'5918','',[fixture('unknown')]);await settle();choose(uncertain,'Alpha');
  assert.strictEqual(uncertain.get('heroStatusLabel').textContent,'Checking status');
  assert.strictEqual(uncertain.get('preparationTrack').getAttribute('aria-valuenow'),null);
  assert.strictEqual(uncertain.postCount(),0);
  console.log(JSON.stringify({passed:true,cases:8,actualEmbeddedPage:true,matchingCardProgressOnly:true,globalNotifications:false,consoleContacted:false}));
}
check().catch(error=>{console.error(error);process.exitCode=1;});
