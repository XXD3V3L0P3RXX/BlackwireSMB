// Chrome extension only. No browser launch, console contact or page mutation
// through evaluation. The supplied fixture API is restricted to loopback.
async function verifyBlackwireCombinedChrome(context) {
  const tab=context.tab,p=tab.playwright,origin=new URL(await tab.url()).origin;
  if(new URL(origin).hostname!=='127.0.0.1')throw Error('Use the isolated loopback fixture.');
  const checks=[];
  function check(ok,label){if(!ok)throw Error(label);checks.push(label);}
  const selected=()=>p.locator('#selectedGame .game-details:not([hidden])');
  const title=()=>selected().locator('.title').textContent();
  const play=()=>selected().locator('.actions button').first();
  const send=()=>selected().locator('.actions button').nth(1);
  const cover=name=>p.locator('#games .cover-choice').filter({hasText:name});
  const covers=()=>p.locator('#games .game:not([hidden])');
  async function reset(scenario='idle',options={}){
    await context.fixture({scenario,options});await tab.reload();
    await p.locator(scenario==='empty'?'#empty':'#selectedGame .game-details:not([hidden]) .title').waitFor({state:'visible'});
  }
  check((await context.fixture()).fixture==='blackwire-combined-ui-preview-v2','Isolated combined protocol fixture');
  await context.viewport.set({width:1440,height:900});await reset();
  check(await covers().count()===10,'Combined PC and console inventory');
  check(await title()==='Console Northstar'&&!await selected().locator('.action').nth(1).isVisible(),'Console source offers Play only');
  await p.locator('#filter-pc').click();check(await covers().count()===7,'PC filter excludes console copies');
  await cover('Northstar').filter({hasNotText:'Console'}).click();
  check(!await send().isVisible()&&await play().isVisible()&&await play().isEnabled(),'Ready PC source offers Play only');
  await cover('Ashore').click();
  check(await send().isVisible()&&await send().isEnabled()&&!await play().isVisible(),'Unready PC source offers Send files only');
  await send().dblclick();await p.locator('#sessionHeading').filter({hasText:'Operation accepted'}).waitFor({state:'visible'});
  let calls=(await context.fixture()).calls;
  check(calls.length===1&&calls[0].route==='/api/dashboard/action'&&calls[0].body.kind===1&&calls[0].body.action==='send','Double Send emits one typed action');
  check(calls[0].body.catalogGeneration===9&&!Object.hasOwn(calls[0].body,'name')&&calls[0].token==='f'.repeat(32),'Exact generation, opaque identity and shared token');
  check(await send().isVisible()&&await send().getAttribute('aria-disabled')==='true','Active PC Send remains visible and unavailable');
  await context.fixture({finishOperation:true});await p.locator('#sessionRefresh').click();
  check(!await send().isVisible()&&await play().isVisible()&&await play().isEnabled(),'Completed preparation changes Send files to Play');
  await p.locator('#filter-console').click();check(await covers().count()===3,'Console filter excludes PC streams');
  await cover('Moonlit Arcade').click();await play().dblclick();
  await p.locator('#sessionHeading').filter({hasText:'Operation accepted'}).waitFor({state:'visible'});
  calls=(await context.fixture()).calls;
  check(calls.length===2&&calls[1].route==='/api/dashboard/action'&&calls[1].body.kind===3,'Installed package uses the same typed Play route once');
  check(calls.every(call=>!['/api/start','/api/play','/api/send'].includes(call.route)),'Combined controls never use old PC launch routes');
  await reset('idle',{waitForFrontend:true});await play().click();
  await p.locator('#sessionHeading').filter({hasText:'Closing dashboard'}).waitFor({state:'visible'});
  check((await p.locator('#sessionOwnership').textContent()).includes('exit normally'),'Browser action waits for normal native exit');
  await reset('idle',{dropAck:true,catalogPatch:{operation:{id:'',itemId:'',phase:'idle',message:'No operation observed.',active:false}}});
  await play().click();await p.locator('#sessionHeading').filter({hasText:'Operation status unknown'}).waitFor({state:'visible'});
  await p.locator('#sessionRefresh').click();
  check(await play().getAttribute('aria-disabled')==='true'&&(await context.fixture()).calls.length===1,'Lost response retains fence; refresh does not replay');
  await context.fixture({options:{catalogPatch:{}}});await p.locator('#sessionRefresh').click();
  await p.locator('#sessionHeading').filter({hasText:'Operation accepted'}).waitFor({state:'visible'});
  check((await context.fixture()).calls.length===1,'Observed exact new nonce confirms operation without resending');
  await reset('offline');check(await title()==='Console Northstar'&&await play().isEnabled(),'PC outage leaves available console source');
  await reset();await p.locator('#filter-ready').click();check(await covers().count()===4,'Ready combines PC startup and console checks');
  await p.getByRole('searchbox').fill('ashore');check(await covers().count()===0,'Search intersects readiness');
  await p.locator('#filter-not-ready').click();check(await covers().count()===1&&await title()==='Ashore','Not ready search finds preparation source');
  await p.locator('#filter-all').click();await p.getByRole('searchbox').fill('PPSA90001');check(await covers().count()===2,'Same title ID keeps distinct PC and console copies');
  await p.getByRole('searchbox').fill('/data/homebrew');check(await covers().count()===2,'Source-path search includes console games');
  await p.getByRole('searchbox').press('Escape');await cover('Signal & Dust').click();
  await cover('Signal & Dust').press('ArrowLeft');check(await title()==='Northstar','Left browses covers');
  await cover('Northstar').filter({hasNotText:'Console'}).press('ArrowRight');check(await title()==='Signal & Dust','Right browses covers');
  await cover('Signal & Dust').press('ArrowDown');check(await play().evaluate(el=>el===document.activeElement),'Down focuses Play');
  await play().press('ArrowDown');check(!await send().isVisible()&&await play().evaluate(el=>el===document.activeElement),'Ready navigation cannot focus hidden Send files');
  await play().press('Escape');check(await cover('Signal & Dust').evaluate(el=>el===document.activeElement),'Escape returns to cover');
  await cover('Stone Circuit').click();await cover('Stone Circuit').press('ArrowDown');
  check(!await play().isVisible()&&await send().evaluate(el=>el===document.activeElement),'Unready navigation focuses Send files directly');
  await send().press('ArrowUp');check(await cover('Stone Circuit').evaluate(el=>el===document.activeElement),'Up skips hidden Play');
  check((await context.fixture()).calls.length===0,'Browsing emits no action');
  await p.locator('#filter-console').click();await cover('Remote Archive').click();
  check(await play().getAttribute('aria-disabled')==='true'&&(await selected().textContent()).includes('Local image launch verification'),'Unsupported local image keeps backend reason');
  await p.locator('#filter-all').click();await cover('Stone Circuit').click();await cover('Stone Circuit').locator('.cover-label').waitFor({state:'visible'});
  check(!await cover('Stone Circuit').locator('img').isVisible(),'Missing cover has initials fallback');
  await cover('The Longest Journey Through the Silent Valley').click();
  for(const size of [{width:1920,height:1080},{width:1440,height:900},{width:1366,height:768},{width:768,height:1024},{width:390,height:844},{width:320,height:740}]){
    await context.viewport.set(size);await p.locator('#filter-all').press('Control+Home');
    const bounds=await p.evaluate(()=>({width:innerWidth,height:innerHeight,scrollWidth:document.documentElement.scrollWidth,details:document.getElementById('selectedGame').getBoundingClientRect().toJSON(),actions:document.querySelector('#selectedGame .game-details:not([hidden]) .actions').getBoundingClientRect().toJSON()}));
    check(bounds.scrollWidth<=bounds.width+1&&bounds.details.left>=0&&bounds.details.right<=bounds.width+1,'No horizontal overflow for '+size.width+'px request');
    if(bounds.width>=1121)check(bounds.actions.bottom<=bounds.height,'Desktop actions fit actual '+bounds.width+'px CSS viewport');
    await context.saveScreenshot('chrome-'+size.width+'.png',await tab.getScreenshot({emit:false}));
  }
  await context.viewport.reset();
  return {passed:checks.length,checks,scope:'Chrome extension with combined protocol fixture only. No console, live service handler, SMB, hardware controller or game runtime is exercised.'};
}
