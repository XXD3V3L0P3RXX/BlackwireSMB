// Exercise the actual embedded page across an accepted check and resident restart.
const assert=require('assert');
const {createPage,nativeState,clone,settle}=require('../../src/blackwire/desk/tests/setup_dom_harness.js');
async function main(){
  const old=nativeState(),checking=clone(old),restarted=clone(old);
  Object.assign(checking.setup,{editable:false,busy:true,operation:5,phase:'checking',message:'Checking connection.'});
  Object.assign(restarted.setup,{token:'e'.repeat(32),revision:1,operation:0,phase:'idle'});
  const app=createPage([old,checking,restarted,clone(restarted)]);
  await settle();app.get('shareName').value='UpdatedLibrary';app.get('shareName').fire('input');
  app.replies.push({ok:true,pending:true,operation:5,message:'Checking connection.'});
  app.get('setup').fire('submit');await settle();
  assert.strictEqual(app.postCount(),1);assert(app.get('server').disabled);
  await app.poll();
  assert.strictEqual(app.get('server').disabled,false,'New resident leaves the form locked on the old operation');
  assert.strictEqual(app.get('reloadSetup').disabled,false,'New resident must allow an explicit status refresh');
  assert(app.get('saveSetup').disabled,'A restart must not authorize resubmission');
  assert.strictEqual(app.get('shareName').value,'UpdatedLibrary','Restart discarded a user draft');
  assert.strictEqual(app.postCount(),1,'A restart automatically replayed the old request');
  app.get('reloadSetup').click();await settle();
  assert.strictEqual(app.get('shareName').value,'UpdatedLibrary');assert.strictEqual(app.postCount(),1);
  assert(!app.get('server').disabled);assert(!app.get('saveSetup').disabled);
  console.log(JSON.stringify({passed:true,residentRestartReleasesStaleClientWatch:true,draftPreserved:true,automaticReplay:false}));
}
main().catch(e=>{console.error(e);process.exitCode=1;});
