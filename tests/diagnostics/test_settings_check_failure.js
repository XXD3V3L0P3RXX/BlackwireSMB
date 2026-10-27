// Exercise exact server-provided diagnostics in the actual embedded setup UI.
const assert=require('assert');
const {createPage,nativeState,clone,settle}=require('../../src/blackwire/desk/tests/setup_dom_harness.js');
function setupRequests(app){return app.calls.filter(call=>call.url==='/api/setup').map(call=>JSON.parse(call.options.body));}
async function failedSecondaryCheck(options={}){
  const saved=nativeState();saved.settings.share2='SecondaryLibrary';
  const checking=clone(saved);Object.assign(checking.setup,{operation:5,phase:'checking',busy:true,editable:false});
  const failed=clone(saved);Object.assign(failed.setup,{operation:5,phase:'error',busy:false,editable:true,
    message:'Shared folder not found. Saved settings were not replaced.'});
  const retrying=clone(saved);Object.assign(retrying.setup,{operation:6,phase:'checking',busy:true,editable:false});
  const app=createPage([saved,checking,failed,retrying]);await settle();
  if(options.changeAccountBeforeFirst){
    app.get('smbUser').value='project-account';app.get('smbUser').fire('input');
    app.get('smbPassword').value='fixture-new-password';app.get('smbPassword').fire('input');
  }
  app.get('shareName2').value='MissingLibrary';app.get('shareName2').fire('input');
  app.replies.push({ok:true,pending:true,operation:5,message:'Checking.'});
  app.get('setup').fire('submit');await settle();
  assert.strictEqual(app.get('smbUser').value,options.changeAccountBeforeFirst?'project-account':'blackwire',
    'Accepting a pending check must retain the nonsecret username draft');
  assert.strictEqual(app.get('smbPassword').value,'','Accepted checks must still clear typed passwords');
  await app.poll();
  assert.strictEqual(app.get('shareName2').value,'MissingLibrary','Failed check discarded the share draft');
  assert.strictEqual(app.get('smbUser').value,options.changeAccountBeforeFirst?'project-account':'blackwire');
  assert.strictEqual(app.get('keepCredentials').checked,!options.changeAccountBeforeFirst);
  app.get('shareName2').value='SecondaryLibrary';app.get('shareName2').fire('input');
  if(options.changeServerAfterFailure){app.get('server').value='192.0.2.42';app.get('server').fire('input');}
  if(options.changeAccountAfterFailure){app.get('smbUser').value='another-project-account';app.get('smbUser').fire('input');}
  app.replies.push({ok:true,pending:true,operation:6,message:'Checking.'});
  app.get('setup').fire('submit');await settle();
  assert.strictEqual(app.postCount(),2,'Retry must require a second explicit submission');
  return setupRequests(app);
}
async function main(){
  for(const configured of [true,false]) {
    const failed=nativeState();
    const message='Shared folder "PS5Games": The share connected, but its game catalog could not be read. Stage: catalog; reason: source_scan_expired. Saved settings were not replaced.';
    Object.assign(failed.setup,{phase:'error',message,configured,busy:false,editable:true});
    Object.assign(failed.library,{phase:'error',stale:true});failed.share.shared=false;
    if(!configured) {failed.settings.server='';failed.settings.share='';}
    const app=createPage([failed]);await settle();
    assert.strictEqual(app.get('savedConnectionStatus').textContent,message);
    assert(!app.get('server').disabled,'A failed check must remain editable');
    assert.strictEqual(app.postCount(),0,'Displaying the failure must not retry it');
  }
  const reused=await failedSecondaryCheck();
  for(const body of reused){
    assert.strictEqual(body.keepCredentials,true,'Correcting only a share must retain saved sign-in');
    assert(!Object.hasOwn(body,'smbUser')&&!Object.hasOwn(body,'smbPassword'),'Saved credentials must not be copied into the request');
  }
  for(const options of [{changeServerAfterFailure:true},{changeAccountAfterFailure:true},{changeAccountBeforeFirst:true}]){
    const requests=await failedSecondaryCheck(options),retry=requests[1];
    assert.strictEqual(retry.keepCredentials,false,'A changed PC/account must not reuse saved credentials');
    assert.strictEqual(retry.smbPassword,'','A previous typed password must not survive into a retry');
    assert.strictEqual(retry.smbUser,options.changeAccountAfterFailure?'another-project-account':options.changeAccountBeforeFirst?'project-account':'blackwire');
    if(options.changeAccountBeforeFirst){
      assert.strictEqual(requests[0].keepCredentials,false);assert.strictEqual(requests[0].smbPassword,'fixture-new-password');
    }
  }
  console.log(JSON.stringify({passed:true,exactFailureVisibleWithAndWithoutSavedConfig:true,
    failedCheckRetainsUsernameDraft:true,secondaryOnlyRetryRetainsSavedSignIn:true,
    changedPcOrAccountNeverReusesCredentials:true,typedPasswordStillCleared:true,automaticRetry:false}));
}
main().catch(e=>{console.error(e);process.exitCode=1;});
