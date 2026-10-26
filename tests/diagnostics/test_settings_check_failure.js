// Exercise exact server-provided diagnostics in the actual embedded setup UI.
const assert=require('assert');
const {createPage,nativeState,settle}=require('../../src/blackwire/desk/tests/setup_dom_harness.js');
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
  console.log(JSON.stringify({passed:true,exactFailureVisibleWithAndWithoutSavedConfig:true,automaticRetry:false}));
}
main().catch(e=>{console.error(e);process.exitCode=1;});
