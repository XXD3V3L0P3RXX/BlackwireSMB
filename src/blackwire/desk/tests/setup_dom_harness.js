// Offline regression of the actual embedded app. All fetches and timers are in memory.
const assert = require('assert');
const fs = require('fs');
const vm = require('vm');
const page = fs.readFileSync(process.argv[2], 'utf8');
const script = id => page.match(new RegExp('<script id="' + id + '">([\\s\\S]*?)</script>'))[1];
const clone = value => JSON.parse(JSON.stringify(value));
const settle = () => new Promise(resolve => setImmediate(resolve));
function nativeState() {
  return {backend:'native',settings:{server:'192.0.2.10',share:'GameLibrary',user:'blackwire',passwordSaved:true},games:[],jobs:[],
    setup:{supported:true,editable:true,busy:false,configured:true,hasSavedCredentials:true,
      revision:3,token:'f'.repeat(32),operation:0,phase:'saved',message:'PC share connected.'},
    library:{phase:'ready',generation:7,stale:false},share:{shared:true},stream:{phase:'stopped'}};
}
function catalog() {
  return {schema:1,catalogGeneration:9,pcConnected:true,consoleConnected:true,items:[],
    operation:{id:'',itemId:'',phase:'idle',message:'',active:false}};
}
function createPage(states, port='5918', hash='', dashboardStates=null, options={}) {
  const elements = new Map(), timers = new Map(), calls = [], replies = [], windowListeners = {};
  const documentListeners={},preferences=options.preferences || new Map();
  let nextTimer = 1;
  const document = {hidden:false,hasFocus:()=>true,addEventListener(type,listener){(documentListeners[type] ||= []).push(listener);}};
  class Element {
    constructor(tag, attrs='') {
      this.tagName=tag.toUpperCase();this.children=[];this.listeners={};this.attributes={};
      this.hidden=/\bhidden(?:\s|>|$)/.test(attrs);this.disabled=/\bdisabled(?:\s|>|$)/.test(attrs);
      this.value='';this.checked=false;this.open=false;this._text='';this.focusCount=0;this.scrollCount=0;this.style={};this.scrollLeft=0;
    }
    set textContent(value) {this._text=String(value);this.children=[];}
    get textContent() {return this._text;}
    setAttribute(key,value) {this.attributes[key]=String(value);}
    getAttribute(key) {return this.attributes[key] ?? null;}
    removeAttribute(key) {delete this.attributes[key];}
    appendChild(element) {element.parentElement=this;this.children.push(element);return element;}
    insertBefore(element,before) {
      if(element.parentElement)element.remove();
      const index=before ? this.children.indexOf(before) : this.children.length;
      assert(index>=0);element.parentElement=this;this.children.splice(index,0,element);return element;
    }
    remove() {if(this.parentElement){const list=this.parentElement.children;list.splice(list.indexOf(this),1);this.parentElement=null;}}
    contains(element) {return element===this || this.children.some(child=>child.contains(element));}
    addEventListener(type, listener) {(this.listeners[type] ||= []).push(listener);}
    fire(type) {for(const fn of this.listeners[type] || [])fn.call(this,{preventDefault(){}});}
    click() {if(!this.disabled)this.fire('click');}
    focus() {if(!this.disabled){document.activeElement=this;this.focusCount++;}}
    scrollIntoView() {this.scrollCount++;}
    getBoundingClientRect() {return {left:0,top:0,width:200,height:300,right:200,bottom:300};}
  }
  document.body=new Element('body');document.activeElement=document.body;
  document.createElement=tag=>new Element(tag);
  document.getElementById=id=>{assert(elements.has(id),'Missing actual HTML element: '+id);return elements.get(id);};
  for(const match of page.matchAll(/<([a-z][a-z0-9]*)\b([^>]*\bid="([^"]+)"[^>]*)>/gi))
    elements.set(match[3],new Element(match[1],match[2]));
  const reply = value => ({ok:true,status:200,json:()=>Promise.resolve(clone(value))});
  let closeCount=0;
  const context = {document,navigator:{userAgent:options.userAgent || ''},location:{port,pathname:'/',hash,hostname:options.hostname || '192.0.2.20'},console,
    close(){closeCount++;if(options.closeThrows)throw new Error('Close refused by browser');},
    localStorage:{getItem:key=>preferences.get(key) ?? null,setItem:(key,value)=>preferences.set(key,value)},
    getComputedStyle:()=>({gridTemplateColumns:Array(options.columns || 6).fill('220px').join(' ')}),
    addEventListener(type,listener){(windowListeners[type] ||= []).push(listener);},setTimeout(fn,delay){const id=nextTimer++;timers.set(id,{fn,delay});return id;},
    clearTimeout(id){timers.delete(id);},fetch(url,options) {
      calls.push({url,options});
      if(url==='/api/state') {
        assert(states.length,'Unexpected status poll');const value=states.shift();
        return value instanceof Error ? Promise.reject(value) : Promise.resolve(reply(value));
      }
      if(url==='/api/dashboard/library') {
        if(!dashboardStates)return Promise.resolve(reply(catalog()));
        assert(dashboardStates.length,'Unexpected dashboard poll');const value=dashboardStates.shift();
        return value instanceof Error ? Promise.reject(value) : Promise.resolve(reply(value));
      }
      if(url==='/api/dashboard/action' || url==='/api/refresh') {
        assert(replies.length,'Unexpected action request');const value=replies.shift();
        if(value instanceof Error)return Promise.reject(value);
        if(typeof value==='function')return value();
        return Promise.resolve({ok:value.status>=200&&value.status<300,status:value.status,json:()=>Promise.resolve(clone(value.body))});
      }
      if(url==='/api/setup') {
        assert(replies.length,'Unexpected setup request');return Promise.resolve(reply(replies.shift()));
      }
      throw new Error('Unexpected request: '+url);
    }};
  context.location.href='http://'+context.location.hostname+':'+port+'/'+hash;
  let historyEntries=[{state:null,url:context.location.href}],historyIndex=0;
  function historyUrl(url){if(url){context.location.href=url;context.location.hash=url.includes('#') ? '#'+url.split('#')[1] : '';}}
  context.history={get state(){return historyEntries[historyIndex].state;},get length(){return historyEntries.length;},
    replaceState(state,title,url){historyEntries[historyIndex]={state:clone(state),url};historyUrl(url);},
    pushState(state,title,url){historyEntries=historyEntries.slice(0,historyIndex+1);historyEntries.push({state:clone(state),url});historyIndex++;historyUrl(url);}};
  context.navigator.getGamepads=()=>options.pad ? [options.pad] : [];
  context.window=context;
  vm.createContext(context);
  const initial = {
    saveDisabled:elements.get('saveSetup').disabled,
    serverVisible:!elements.get('nativeServerField').hidden,
    shareVisible:!elements.get('nativeShareField').hidden,
  };
  vm.runInContext(script('blackwire-view-model')+'\n'+script('blackwire-app'),context);
  return {elements,calls,replies,initial,states,preferences,closeCount:()=>closeCount,get:id=>elements.get(id),focused:()=>document.activeElement,postCount:()=>calls.filter(call=>call.options?.method==='POST').length,
    historyLength:()=>context.history.length,
    async back(eventState){assert(historyIndex>0,'No previous history entry');historyIndex--;historyUrl(historyEntries[historyIndex].url);for(const listener of windowListeners.popstate || [])listener({state:eventState===undefined ? context.history.state : eventState});await settle();},
    async initialPop(){for(const listener of windowListeners.popstate || [])listener({state:context.history.state});await settle();},
    key(key,extra={}) {
      const event={key,repeat:false,shiftKey:false,prevented:false,preventDefault(){this.prevented=true;},...extra};
      for(const listener of documentListeners.keydown || [])listener(event);
      return event;
    },
    async navigate(hash) {
      context.location.hash=hash;
      for(const listener of windowListeners.hashchange || [])listener();
      await settle();
    },
    async poll() {
      const entry=[...timers].find(([,timer])=>timer.delay===3000);assert(entry,'No scheduled status poll');
      timers.delete(entry[0]);entry[1].fn();await settle();
    },
    async runTimers(delay) {
      const ready=[...timers].filter(([,timer])=>timer.delay===delay);assert(ready.length,'No timer with delay '+delay);
      for(const [id,timer] of ready)if(timers.delete(id))timer.fn();
      await settle();
    }};
}
async function check() {
  for(const obsolete of ['folder','ps5','browse','pcFolderField','ps5Field'])
    assert(!new RegExp('id="'+obsolete+'"').test(page),'Obsolete setup control: '+obsolete);
  assert(!/desktop service|PC service|Browse on PC|PS5 address|Games folder on the PC/.test(page));
  for(const id of ['server','shareName','smbUser','smbPassword']) {
    const input=page.match(new RegExp('<input[^>]*id="'+id+'"[^>]*>'))[0];
    assert(!/\b(?:value|placeholder)=/.test(input),'Setup must start visibly blank: '+id);
  }
  assert(!/\bchecked(?:\s|>)/.test(page.match(/<input[^>]*id="keepCredentials"[^>]*>/)[0]));
  const entered={server:'192.0.2.25',shareName:'DraftLibrary',smbUser:'project-account',smbPassword:'fixture-password'};
  function assertBlank(app) {
    for(const id of Object.keys(entered))assert.strictEqual(app.get(id).value,'','Setup unexpectedly filled '+id);
    assert.strictEqual(app.get('keepCredentials').checked,false);
  }
  function assertSaved(app) {
    assert.strictEqual(app.get('server').value,'192.0.2.10');assert.strictEqual(app.get('shareName').value,'GameLibrary');
    assert.strictEqual(app.get('smbUser').value,'blackwire');assert.strictEqual(app.get('smbPassword').value,'');
    assert.strictEqual(app.get('smbPassword').placeholder,'********');assert.strictEqual(app.get('keepCredentials').checked,true);
    assert.strictEqual(app.get('savedPassword').textContent,'********');assert.strictEqual(app.get('savedUser').textContent,'blackwire');
  }
  function enterDraft(app) {
    for(const [id,value] of Object.entries(entered)) {app.get(id).value=value;app.get(id).fire('input');}
  }
  function assertDraft(app) {
    for(const [id,value] of Object.entries(entered))assert.strictEqual(app.get(id).value,value,'Draft changed: '+id);
  }

  const first=createPage([new Error('Connection refused')]);
  assert(first.initial.saveDisabled && first.initial.serverVisible && first.initial.shareVisible);
  assertBlank(first);
  assert(first.get('setupReason').textContent.includes('Connecting to the PS5 app'));
  await settle();
  assert(first.get('saveSetup').disabled && !first.get('saveSetup').hidden);
  assert(first.get('setupReason').textContent.includes('Connection refused'));
  assert(!first.get('nativeServerField').hidden && !first.get('nativeShareField').hidden);
  first.get('setup').fire('submit');assert.strictEqual(first.postCount(),0);
  const freshState=nativeState();freshState.settings={server:'',share:'',user:'',passwordSaved:false};
  Object.assign(freshState.setup,{configured:false,hasSavedCredentials:false});freshState.library={phase:'needs_setup',generation:0,stale:false};
  const fresh=createPage([freshState]);await settle();assertBlank(fresh);assert.strictEqual(fresh.get('savedConnectionStatus').textContent,'Not configured');
  fresh.get('setup').fire('submit');assert.strictEqual(fresh.postCount(),0);
  const existing=createPage([nativeState(),nativeState()]);await settle();assertSaved(existing);
  assert.strictEqual(existing.get('savedConnectionStatus').textContent,'Connected (last share check)');
  assert.strictEqual(existing.postCount(),0);await existing.poll();assertSaved(existing);assert.strictEqual(existing.postCount(),0);

  for(const invalid of [{}, {...nativeState(),backend:'desktop'}, {...nativeState(),setup:{editable:true}},
    {...nativeState(),setup:{...nativeState().setup,token:'0'.repeat(32)}}]) {
    // Every origin requires the same positively identified native backend.
    const app=createPage([invalid],'23456');await settle();
    assert(app.get('saveSetup').disabled);assert(app.get('server').disabled);
    assert(!app.get('nativeServerField').hidden && !app.get('nativeShareField').hidden);
    app.get('setup').fire('submit');assert.strictEqual(app.postCount(),0);
    assert(!app.calls.some(call=>call.url==='/api/dashboard/library'));
  }

  const busy=nativeState();Object.assign(busy.setup,{editable:false,busy:true,message:'Checking the PC share.'});
  const recovery=createPage([busy,nativeState(),nativeState(),new Error('Console disconnected'),nativeState()]);
  await settle();assert(recovery.get('saveSetup').disabled);assert.strictEqual(recovery.get('setupReason').textContent,'Checking the PC share.');
  await recovery.poll();assert(!recovery.get('saveSetup').disabled);assert(!recovery.get('server').disabled);
  assertSaved(recovery);assert.strictEqual(recovery.postCount(),0);
  recovery.get('openSetup').click();assert.strictEqual(recovery.focused(),recovery.get('server'));
  enterDraft(recovery);await recovery.poll();assertDraft(recovery);assert.strictEqual(recovery.get('keepCredentials').checked,false);
  await recovery.poll();assert(recovery.get('saveSetup').disabled);assert(recovery.get('setupReason').textContent.includes('Console disconnected'));
  assertDraft(recovery);
  recovery.get('setup').fire('submit');assert.strictEqual(recovery.postCount(),0);
  await recovery.poll();assert(!recovery.get('saveSetup').disabled);assertDraft(recovery);

  const rotated=nativeState();rotated.setup.token='e'.repeat(32);rotated.setup.revision=4;
  const checking=clone(rotated);Object.assign(checking.setup,{editable:false,busy:true,operation:5,phase:'checking'});
  const saved=clone(rotated);saved.setup.operation=5;
  saved.settings={server:'192.0.2.25',share:'DraftLibrary',user:'project-account',passwordSaved:true};
  const draft=createPage([nativeState(),rotated,clone(rotated),checking,clone(checking),saved]);
  await settle();assertSaved(draft);enterDraft(draft);
  await draft.poll();assert(draft.get('saveSetup').disabled);
  assert(draft.get('setupReason').textContent.includes('Saved settings changed'));
  assertDraft(draft);
  draft.get('setup').fire('submit');assert.strictEqual(draft.postCount(),0);
  draft.get('reloadSetup').click();assert(draft.get('saveSetup').disabled);assertDraft(draft);
  await settle();assert(!draft.get('saveSetup').disabled);assertDraft(draft);
  assert.strictEqual(draft.get('keepCredentials').checked,false);
  draft.replies.push({ok:true,pending:true,operation:5,message:'Checking the PC connection.'});
  draft.get('setup').fire('submit');draft.get('setup').fire('submit');await settle();
  assert.strictEqual(draft.postCount(),1);assert(draft.get('saveSetup').disabled);
  const post=draft.calls.find(call=>call.url==='/api/setup');
  assert.strictEqual(post.options.headers['X-Blackwire-Token'],'e'.repeat(32));
  const body=JSON.parse(post.options.body);assert.strictEqual(body.revision,4);
  assert.strictEqual(body.server,entered.server);assert.strictEqual(body.share,entered.shareName);
  assert.strictEqual(body.keepCredentials,false);assert.strictEqual(body.smbUser,entered.smbUser);assert.strictEqual(body.smbPassword,entered.smbPassword);
  assert(!Object.hasOwn(body,'gamesFolder') && !Object.hasOwn(body,'ps5Address'));
  assert(draft.get('setupReason').textContent.includes('Waiting for this connection check'));
  await draft.poll();draft.get('setup').fire('submit');assert.strictEqual(draft.postCount(),1);
  await draft.poll();assert(!draft.get('saveSetup').disabled);assert.strictEqual(draft.postCount(),1);
  assert.strictEqual(draft.get('server').value,entered.server);assert.strictEqual(draft.get('shareName').value,entered.shareName);
  assert.strictEqual(draft.get('smbUser').value,'project-account');assert.strictEqual(draft.get('smbPassword').value,'');
  assert.strictEqual(draft.get('keepCredentials').checked,true);assert.strictEqual(draft.get('smbPassword').placeholder,'********');

  const entry=createPage([busy,nativeState(),nativeState()],'5918','#setup');
  assert(entry.get('setupPanel').open);assert.strictEqual(entry.get('setupPanel').scrollCount,1);assertBlank(entry);
  await settle();assert(entry.get('setupPanel').open);assert.strictEqual(entry.get('server').focusCount,0);
  await entry.poll();assert.strictEqual(entry.focused(),entry.get('server'));assert.strictEqual(entry.get('server').focusCount,1);
  entry.get('search').focus();await entry.poll();assert.strictEqual(entry.focused(),entry.get('search'));
  assert.strictEqual(entry.get('server').focusCount,1);assert.strictEqual(entry.get('setupPanel').scrollCount,1);assertSaved(entry);
  assert.strictEqual(entry.postCount(),0);

  const hashEntry=createPage(Array.from({length:5},nativeState));await settle();
  assert(!hashEntry.get('setupPanel').open);enterDraft(hashEntry);
  await hashEntry.navigate('#setup');assert(hashEntry.get('setupPanel').open);assertDraft(hashEntry);
  assert.strictEqual(hashEntry.get('server').focusCount,1);assert.strictEqual(hashEntry.get('setupPanel').scrollCount,1);
  hashEntry.get('search').focus();await hashEntry.navigate('#setup');
  assert.strictEqual(hashEntry.focused(),hashEntry.get('search'));assert.strictEqual(hashEntry.get('server').focusCount,1);
  await hashEntry.navigate('#unrelated');await hashEntry.navigate('#setup');assertDraft(hashEntry);
  assert.strictEqual(hashEntry.get('server').focusCount,2);assert.strictEqual(hashEntry.get('setupPanel').scrollCount,2);
  assert.strictEqual(hashEntry.postCount(),0);

  const reconnectEntry=createPage([nativeState(),new Error('Disconnected before settings'),nativeState(),nativeState()]);
  await settle();enterDraft(reconnectEntry);await reconnectEntry.poll();assertDraft(reconnectEntry);
  const recovering=reconnectEntry.navigate('#setup');assert.strictEqual(reconnectEntry.get('server').focusCount,0);
  await recovering;assert.strictEqual(reconnectEntry.focused(),reconnectEntry.get('server'));assertDraft(reconnectEntry);
  reconnectEntry.get('search').focus();await reconnectEntry.poll();assert.strictEqual(reconnectEntry.focused(),reconnectEntry.get('search'));
  assert.strictEqual(reconnectEntry.get('server').focusCount,1);assert.strictEqual(reconnectEntry.postCount(),0);
  const two=nativeState();two.settings.share2='CompressedLibrary';
  const checkingTwo=clone(two);Object.assign(checkingTwo.setup,{editable:false,busy:true,operation:6,phase:'checking'});
  const twoFolders=createPage([two,checkingTwo]);await settle();
  assert.strictEqual(twoFolders.get('shareName').value,'GameLibrary');assert.strictEqual(twoFolders.get('shareName2').value,'CompressedLibrary');
  assert.strictEqual(twoFolders.get('savedShare').textContent,'GameLibrary + CompressedLibrary');
  twoFolders.get('shareName2').value='';twoFolders.get('shareName2').fire('input');
  assert(twoFolders.get('keepCredentials').checked);
  twoFolders.replies.push({ok:true,pending:true,operation:6,message:'Checking both folders.'});
  twoFolders.get('setup').fire('submit');await settle();
  assert.strictEqual(twoFolders.postCount(),1);
  const twoBody=JSON.parse(twoFolders.calls.find(call=>call.url==='/api/setup').options.body);
  assert.strictEqual(twoBody.share,'GameLibrary');assert.strictEqual(twoBody.share2,'');assert.strictEqual(twoBody.keepCredentials,true);
  assert(!Object.hasOwn(twoBody,'smbPassword'));
  console.log('Offline setup DOM: fresh blank fields, loaded saved nonsecret fields and fixed password marker, retained drafts over polls/reconnect/status refresh, stale token, wrong backend, busy recovery and one-shot save passed.');
}
if(require.main===module)check().catch(error=>{console.error(error);process.exitCode=1;});
module.exports={createPage,nativeState,catalog,clone,settle};
