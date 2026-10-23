"""Behavior checks for the one UI embedded and served by the console."""

import json
from html.parser import HTMLParser
from pathlib import Path
import re
import shutil
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[2]
PAGE = (ROOT / "desk" / "index.html").read_text(encoding="utf-8")
MODEL = re.search(r'<script id="blackwire-view-model">([\s\S]*?)</script>', PAGE).group(1)


@unittest.skipUnless(shutil.which("node"), "Node.js is needed to verify UI behavior")
class SharedUiTests(unittest.TestCase):
    def test_modal_dialogs_are_outside_the_inert_library_background(self):
        class Ancestry(HTMLParser):
            def __init__(self):
                super().__init__()
                self.stack = []
                self.ancestors = {}

            def handle_starttag(self, tag, attributes):
                identity = dict(attributes).get("id")
                if identity:
                    self.ancestors[identity] = [name for _, name in self.stack if name]
                if tag not in {"area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "param", "source", "track", "wbr"}:
                    self.stack.append((tag, identity))

            def handle_endtag(self, tag):
                for index in range(len(self.stack) - 1, -1, -1):
                    if self.stack[index][0] == tag:
                        del self.stack[index:]
                        return

        document = Ancestry()
        document.feed(PAGE)
        for identity in ("selectedGame", "settingsOverlay", "helpOverlay", "removalConfirm"):
            self.assertIn(identity, document.ancestors)
            self.assertNotIn("librarySurface", document.ancestors[identity],
                             "Opening a dialog must not make its own controls inert")
        self.assertIn("selectedGame", document.ancestors["removalConfirm"])

    def test_exact_preview_notifications_stay_on_their_matching_game(self):
        result = subprocess.run(["node", str(ROOT / "desk/tests/exact_preview_dom_harness.js"),
                                 str(ROOT / "desk/index.html")], text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_redesign_hero_dispatch_overlay_focus_and_no_implicit_actions(self):
        result = subprocess.run(["node", str(ROOT / "desk/tests/redesign_dom_harness.js"),
                                 str(ROOT / "desk/index.html")], text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_visible_rescan_and_console_specific_hints_and_back(self):
        result = subprocess.run(["node", str(ROOT / "desk/tests/library_controls_dom_harness.js"),
                                 str(ROOT / "desk/index.html")], text=True, capture_output=True, encoding="utf-8")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_dashboard_setup_failure_keeps_prepared_play_without_resending(self):
        self.behavior('''
const c=catalogFixture(),item=c.items[0];item.startupReady=true;item.canPlay=true;item.canSend=false;
const original={name:item.source,titleId:item.titleId,dashboardLaunchReady:false,dashboardLaunchReason:'Dashboard setup did not finish.'};
const s={backend:'native',dashboard:c,games:[original],jobs:[],stream:{phase:'stopped'}};
let homeRow=ui.libraryGames(s)[0],homeAction=ui.actions(homeRow,s,null);
assert.strictEqual(homeRow.dashboardLaunchReady,false);assert(homeAction.canPlay);assert(!homeAction.showPrepare);
assert(!ui.ready(homeRow));assert.strictEqual(ui.counts(ui.libraryGames(s),'').ready,1);
assert.strictEqual(homeAction.playLabel,'Finish setup and play');assert.strictEqual(ui.dashboardBody(s,homeRow,'play').action,'play');
original.dashboardLaunchReady=true;homeRow=ui.libraryGames(s)[0];assert.strictEqual(ui.actions(homeRow,s,null).playLabel,'Play');
assert(ui.ready(homeRow));assert.strictEqual(ui.counts(ui.libraryGames(s),'').ready,2);
original.dashboardLaunchReady=null;homeRow=ui.libraryGames(s)[0];assert.strictEqual(homeRow.dashboardLaunchReady,null);assert.strictEqual(ui.actions(homeRow,s,null).playLabel,'Play');
original.dashboardLaunchReady=false;original.name='Another copy';homeRow=ui.libraryGames(s)[0];
assert.strictEqual(homeRow.dashboardLaunchReady,undefined);assert.strictEqual(ui.actions(homeRow,s,null).playLabel,'Play');
''')

    def test_failed_dashboard_provision_is_attention_without_duplicate_send(self):
        self.preparation_behavior('''
const notices=ui.preparationNotifications();notices.observe(ui.preparationPanel(s,null),true);
Object.assign(preparation,{jobState:'done',phase:12,files:10,bytes:10485760,ready:true});c.operation.active=false;c.operation.phase='stopped';
c.items[0].startupReady=true;c.items[0].canPlay=true;c.items[0].canSend=false;
s.games=[{name:c.items[0].source,titleId:c.items[0].titleId,dashboardLaunchReady:false,dashboardLaunchReason:'Dashboard setup needs attention.'}];
const panel=ui.preparationPanel(s,null);assert.strictEqual(panel.state,'setup-needed');assert(!panel.busy);assert.strictEqual(panel.percent,100);
assert(!ui.dashboardBusy(s));assert(panel.message.includes('no files need to be sent again'));
const row=ui.libraryGames(s)[0],action=ui.actions(row,s,null);assert(action.canPlay);assert(!action.showPrepare);
const result=notices.observe(panel,true);assert(result.attention);assert(result.title.includes('needs dashboard setup'));
''')

    def preparation_behavior(self, assertions):
        self.behavior('''
const preparation={jobId:'c'.repeat(32),jobState:'running',message:'Copying verified startup files',observed:true,phase:7,files:5,filesTotal:10,bytes:5242880,bytesTotal:10485760,ready:false};
const c=catalogFixture();c.items[0].startupReady=false;c.items[0].canPlay=false;c.items[0].canSend=true;
c.operation={id:'a'.repeat(32),itemId:c.items[0].id,action:'send',title:'Preserved PC Game',phase:'running',message:'Preparing files',active:true,exitRequested:false,preparation};
const s={backend:'native',dashboard:ui.dashboardLibrary(c),games:[],jobs:[],stream:{phase:'stopped'}};
''' + assertions)

    def test_preparation_has_named_measured_progress_and_names_competing_work(self):
        self.preparation_behavior('''
const panel=ui.preparationPanel(s,null);assert(panel.visible&&panel.busy);assert.strictEqual(panel.title,'Preserved PC Game');
assert.strictEqual(panel.message,'Copying and verifying startup files');assert.strictEqual(panel.percent,50);assert(panel.detail.includes('5 / 10 files'));
const rows=ui.libraryGames(s);for(const row of rows){const a=ui.actions(row,s,null);assert(!a.canPlay&&!a.canPrepare);assert((a.playReason||a.prepareReason).includes('Preserved PC Game'));}
assert.strictEqual(ui.actions(rows[0],s,null).prepareLabel,'Preparing…');assert.strictEqual(ui.actions(rows[0],s,null).badge,'Preparing files');
''')

    def test_scanning_unknown_totals_and_unobserved_progress_are_not_fake_completion(self):
        self.preparation_behavior('''
Object.assign(preparation,{phase:1,files:0,filesTotal:0,bytes:0,bytesTotal:0});
let panel=ui.preparationPanel(s,null);assert.strictEqual(panel.percent,null);assert.strictEqual(panel.message,'Scanning PC files');assert.strictEqual(panel.state,'preparing');
Object.assign(preparation,{phase:7,files:10,filesTotal:10,bytes:100,bytesTotal:100,observed:false});
panel=ui.preparationPanel(s,null);assert.strictEqual(panel.percent,null);assert.strictEqual(panel.state,'preparing');assert(!panel.detail.includes('100'));
''')

    def test_ready_requires_done_and_fresh_same_item_startup_readiness(self):
        self.preparation_behavior('''
Object.assign(preparation,{jobState:'done',phase:12,files:10,bytes:10485760,ready:true});c.operation.active=false;c.operation.phase='stopped';
assert.strictEqual(ui.preparationPanel(s,null).state,'unknown');assert(ui.preparationPanel(s,null).busy);
assert(ui.dashboardBusy(s));for(const row of ui.libraryGames(s)){const action=ui.actions(row,s,null);assert(!action.canPlay&&!action.canPrepare);}
assert.throws(()=>ui.dashboardBody(s,ui.libraryGames(s)[0],'send'),/available action/);
c.items[0].startupReady=true;c.items[0].canPlay=true;c.items[0].canSend=false;
let panel=ui.preparationPanel(s,null);assert.strictEqual(panel.state,'ready');assert.strictEqual(panel.percent,100);assert(!panel.busy);assert(panel.message.includes('Choose Play'));
assert(!ui.actions(ui.libraryGames(s)[0],s,null).showPrepare);assert(ui.actions(ui.libraryGames(s)[0],s,null).canPlay);
c.items[0].id=c.items[0].id.slice(0,-1)+'f';assert.strictEqual(ui.preparationPanel(s,null).state,'unknown');
''')

    def test_terminal_preparation_failure_keeps_named_actual_reason(self):
        self.preparation_behavior('''
Object.assign(preparation,{jobState:'error',phase:13,message:'PC connection was lost.'});c.operation.active=false;c.operation.phase='stopped';
for(let i=0;i<3;i++){const panel=ui.preparationPanel(s,null);assert.strictEqual(panel.state,'error');assert.strictEqual(panel.title,'Preserved PC Game');assert.strictEqual(panel.message,'PC connection was lost.');assert.strictEqual(panel.percent,null);}
''')

    def test_uncertain_send_never_adopts_an_old_terminal_operation(self):
        self.preparation_behavior('''
s.dashboardWatch={id:'',itemId:c.items[0].id,previousId:c.operation.id,action:'send',title:'Waiting game',phase:'unknown',message:'Checking the existing request'};
Object.assign(preparation,{jobState:'done',ready:true});c.operation.active=false;c.operation.phase='stopped';c.items[0].startupReady=true;
const panel=ui.preparationPanel(s,null);assert.strictEqual(panel.state,'unknown');assert.strictEqual(panel.title,'Waiting game');assert.strictEqual(panel.percent,null);assert(panel.busy);
''')

    def test_new_send_acknowledgement_does_not_request_frontend_exit(self):
        self.preparation_behavior('''
const good={ok:true,accepted:true,id:'d'.repeat(32),phase:'accepted',message:'Preparing startup files',action:'send',exitRequested:false};
assert.strictEqual(ui.dashboardAccepted(202,good,'send'),good);
for(const change of [{exitRequested:true},{action:'play'},{exitRequested:'false'}])assert.throws(()=>ui.dashboardAccepted(202,{...good,...change},'send'),/differs/);
assert.strictEqual(ui.sessionPanel(s,null).visible,false);
''')

    def test_named_pending_and_definite_refusal_replace_old_preparation_history(self):
        self.preparation_behavior('''
const pending={action:'send',itemId:c.items[1].id,title:'Another backup',name:'Another backup'};
let panel=ui.preparationPanel(s,pending);assert.strictEqual(panel.state,'requesting');assert.strictEqual(panel.title,'Another backup');assert.strictEqual(panel.percent,null);
s.preparationIssue={visible:true,key:'request:x',itemId:c.items[1].id,title:'Another backup',state:'error',heading:'Preparation refused',message:'The source changed',percent:null,busy:false};
c.operation.active=false;
panel=ui.preparationPanel(s,null);assert.strictEqual(panel.title,'Another backup');assert.strictEqual(panel.message,'The source changed');
c.operation.active=true;panel=ui.preparationPanel(s,null);assert.strictEqual(panel.title,'Preserved PC Game');assert.strictEqual(panel.state,'preparing');
''')

    def test_preparation_notifications_are_passive_deduplicated_transitions(self):
        self.preparation_behavior('''
const notice=ui.preparationNotifications();let panel=ui.preparationPanel(s,null);assert.strictEqual(notice.observe(panel,true),null);
Object.assign(preparation,{jobState:'done',phase:12,files:10,bytes:10485760,ready:true});c.operation.active=false;c.operation.phase='stopped';c.items[0].startupReady=true;
panel=ui.preparationPanel(s,null);assert.strictEqual(notice.observe(panel,false),null);
const result=notice.observe(panel,true);assert(result.title.includes('Preserved PC Game'));assert(!result.attention);
for(let i=0;i<5;i++)assert.strictEqual(notice.observe(panel,true),null);
const initial=ui.preparationNotifications();assert.strictEqual(initial.observe(panel,true),null);
''')

    def test_preparation_extension_rejects_invalid_numbers_identity_and_exit(self):
        self.preparation_behavior('''
for(const change of [{bytes:-1},{bytes:10485761},{files:11},{phase:14},{bytesTotal:Infinity},{jobId:'0'.repeat(32)},{observed:'true'},{jobState:'ready'},{ready:true}]) {
 const altered=JSON.parse(JSON.stringify(c));Object.assign(altered.operation.preparation,change);assert.throws(()=>ui.dashboardLibrary(altered),/preparation status/);
}
const altered=JSON.parse(JSON.stringify(c));altered.operation.exitRequested=true;assert.throws(()=>ui.dashboardLibrary(altered),/preparation status/);
''')

    def test_preparation_fallback_matches_full_source_instead_of_title_id(self):
        self.preparation_behavior('''
delete c.operation.action;
s.jobs=[{id:'b'.repeat(32),action:'send',name:c.items[0].source,state:'running',message:'Copying files',progress:{phase:7,files:1,filesTotal:4,bytes:1,bytesTotal:4}}];
let panel=ui.preparationPanel(s,null);assert.strictEqual(panel.title,c.items[0].title);assert.strictEqual(panel.percent,25);
s.jobs[0].name='Different source';panel=ui.preparationPanel(s,null);assert.strictEqual(panel.title,'Different source');assert.strictEqual(panel.itemId,'');
''')

    def test_failed_play_supersedes_completed_send_history_without_blocking_library(self):
        self.preparation_behavior('''
s.jobs=[{id:'b'.repeat(32),action:'send',name:c.items[0].source,state:'done',filesPrepared:true,
 progress:{phase:12,files:10,filesTotal:10,bytes:10485760,bytesTotal:10485760}}];
c.operation={id:'d'.repeat(32),itemId:c.items[0].id,action:'play',title:c.items[0].title,
 phase:'refused',active:false,message:'registration_ownership_intent_unconfirmed'};
s.stream={phase:'error',game:c.items[0].source,runId:'e'.repeat(32),message:c.operation.message,gamePid:0,readerPid:0,canClose:false};
assert.strictEqual(ui.preparationPanel(s,null).visible,false);assert(!ui.dashboardBusy(s));
const panel=ui.sessionPanel(s,null);assert(panel.visible&&panel.failed&&panel.dismissible);
assert.strictEqual(panel.heading,'Launch failed');assert.strictEqual(panel.source,c.items[0].title);
assert(panel.message.includes('No game was started'));assert.strictEqual(panel.ownershipLabel,'');
// Historical v30 reported the same error as a normal stop. It must still be presented as a failure.
c.operation.phase='stopped';assert.strictEqual(ui.sessionPanel(s,null).heading,'Launch failed');
// A genuinely running Send is retained even if another operation snapshot is older.
s.jobs[0].state='running';assert(ui.preparationPanel(s,null).busy);
''')

    def test_combined_catalog_keeps_console_sources_out_of_legacy_pc_state(self):
        self.behavior('''
const c=catalogFixture();const raw=[{name:'PC/Game',source:'PC/Game',title:'PC Game',titleId:'PPSA90001',sendBytes:42,stayBytes:84,sourceKind:'container'}];
const s={backend:'native',games:raw,dashboard:ui.dashboardLibrary(c)};
const rows=ui.libraryGames(s);assert.strictEqual(rows.length,2);assert.strictEqual(s.games,raw);assert.strictEqual(s.games.length,1);
assert.strictEqual(rows[0].name,c.items[0].id);assert.strictEqual(rows[0].sourceKind,'container');assert.strictEqual(rows[0].sendBytes,42);
assert.strictEqual(rows[1].kind,2);assert.strictEqual(rows[1].sendBytes,undefined);assert.strictEqual(rows[1].sourceKind,undefined);
assert.strictEqual(rows[0].titleId,rows[1].titleId);assert.notStrictEqual(rows[0].name,rows[1].name);
''')

    def test_combined_actions_use_exact_catalog_identity_and_generation(self):
        self.behavior('''
const c=ui.dashboardLibrary(catalogFixture());const s={backend:'native',dashboard:c,games:[]};const rows=ui.libraryGames(s);
for(const [game,action] of [[rows[0],'play'],[rows[1],'play']]) {
 const body=ui.dashboardBody(s,game,action);
 assert.strictEqual(JSON.stringify(body),JSON.stringify({id:game.dashboardId,kind:game.kind,action,catalogGeneration:11}));
 assert(!Object.hasOwn(body,'name'));assert(!Object.hasOwn(body,'source'));assert(!Object.hasOwn(body,'libraryGeneration'));
}
assert.throws(()=>ui.dashboardBody(s,rows[0],'send'),/available action/);
const pending=catalogFixture();pending.items[0].startupReady=false;pending.items[0].canPlay=false;pending.items[0].canSend=true;
const unready={...s,dashboard:ui.dashboardLibrary(pending)},unreadyRow=ui.libraryGames(unready)[0];
assert.strictEqual(ui.dashboardBody(unready,unreadyRow,'send').action,'send');
assert.throws(()=>ui.dashboardBody(unready,unreadyRow,'play'),/available action/);
assert.throws(()=>ui.dashboardBody(s,rows[1],'send'),/available action/);
assert.throws(()=>ui.dashboardBody(s,{...rows[1],kind:1},'play'),/available action/);
assert.throws(()=>ui.dashboardBody({...s,dashboardUnavailable:'offline'},rows[0],'play'),/available action/);
''')

    def test_combined_filters_and_search_keep_pc_and_console_distinct(self):
        self.behavior('''
const rows=ui.libraryGames({dashboard:ui.dashboardLibrary(catalogFixture()),games:[]});
assert(ui.matches(rows[0],'PC/Game','pc'));assert(!ui.matches(rows[0],'','console'));
assert(ui.matches(rows[1],'/data/homebrew','console'));assert(!ui.matches(rows[1],'','pc'));
const counts=ui.counts(rows,'PPSA90001');assert.strictEqual(counts.all,2);assert.strictEqual(counts.pc,1);assert.strictEqual(counts.console,1);assert.strictEqual(counts.ready,2);
rows[0].startupReady=false;assert(ui.matches(rows[0],'','not-ready'));assert(ui.matches(rows[1],'','ready'));
''')

    def test_combined_catalog_rejects_malformed_identity_capability_and_cover_fields(self):
        self.behavior('''
for(const mutate of [c=>c.schema=2,c=>c.catalogGeneration=0,c=>c.catalogGeneration=9007199254740992,c=>c.pcConnected='yes',c=>c.items.push(c.items[0]),
 c=>c.items[0].kind=0,c=>c.items[0].canPlay=1,c=>c.items[1].canSend=true,c=>c.items[0].id='../../app',c=>c.items[0].titleId='bad',
 c=>c.items[0].cover='https://invalid.example/cover.png',c=>c.items[0].playReason=null,c=>c.operation.phase='done',c=>c.operation.active=true,
 c=>c.operation={id:'0'.repeat(32),itemId:c.items[0].id,phase:'running',active:true,message:''}]) {
 const c=catalogFixture();mutate(c);assert.throws(()=>ui.dashboardLibrary(c),/invalid/);
}
''')

    def test_combined_acknowledgement_requires_exact_202_and_operation_nonce(self):
        self.behavior('''
const good={ok:true,accepted:true,id:'a'.repeat(32),phase:'accepted',message:'Operation accepted.'};
assert.strictEqual(ui.dashboardAccepted(202,good),good);
for(const status of [200,201,204])assert.throws(()=>ui.dashboardAccepted(status,good),/not confirmed/);
for(const change of [{ok:false},{accepted:false},{id:'0'.repeat(32)},{id:'A'.repeat(32)},{id:'short'},{phase:'running'},{message:null}])
 assert.throws(()=>ui.dashboardAccepted(202,{...good,...change}),/not confirmed/);
''')

    def test_combined_post_uses_one_shared_token_and_never_retries_an_unknown_response(self):
        self.behavior('''
(async()=>{
 const body={id:catalogFixture().items[1].id,kind:2,action:'play',catalogGeneration:11};let calls=[];
 const reply={ok:true,accepted:true,id:'a'.repeat(32),phase:'waiting-for-dashboard-exit',message:'Waiting for normal exit.'};
 await ui.dashboardPost(body,'f'.repeat(32),(url,options)=>{calls.push({url,options});return Promise.resolve({ok:true,status:202,json:()=>Promise.resolve(reply)});});
 assert.strictEqual(calls.length,1);assert.strictEqual(calls[0].url,'/api/dashboard/action');assert.strictEqual(calls[0].options.headers['X-Blackwire-Token'],'f'.repeat(32));
 assert.strictEqual(calls[0].options.headers['Content-Type'],'application/json');assert.strictEqual(calls[0].options.body,JSON.stringify(body));
 let count=0;await assert.rejects(ui.dashboardPost(body,'f'.repeat(32),()=>{count++;return Promise.reject(Error('network'));}),/network/);assert.strictEqual(count,1);
 let invoked=false;await assert.rejects(ui.dashboardPost(body,'invalid',()=>{invoked=true;}),/Refresh/);assert(!invoked);
 for(const status of [409,503]) {
  try {await ui.dashboardPost(body,'f'.repeat(32),()=>Promise.resolve({ok:false,status,json:()=>Promise.resolve({error:'Refused or uncertain'})}));assert.fail();}
  catch(error){assert.strictEqual(error.status,status);assert.strictEqual(error.uncertain,status>=500);}
 }
})().catch(error=>{console.error(error);process.exitCode=1;});
''')

    def test_operation_watch_releases_only_matching_terminal_nonce_and_never_replays(self):
        self.behavior('''
const id=catalogFixture().items[0].id, watch={id:'a'.repeat(32),itemId:id,previousId:'b'.repeat(32),phase:'accepted',message:'Accepted'};
for(const operation of [{id:'c'.repeat(32),itemId:id,phase:'stopped',active:false,message:''},{id:watch.id,itemId:catalogFixture().items[1].id,phase:'stopped',active:false,message:''}])
 assert.strictEqual(ui.dashboardWatch(watch,operation),watch);
for(const phase of ['accepted','waiting-for-dashboard-exit','running','unknown'])assert(ui.dashboardWatch(watch,{id:watch.id,itemId:id,phase,active:true,message:'Waiting'}));
for(const phase of ['stopped','refused'])assert.strictEqual(ui.dashboardWatch(watch,{id:watch.id,itemId:id,phase,active:false,message:'Ended'}),null);
const unknown={id:'',itemId:id,previousId:'b'.repeat(32),phase:'unknown',message:'Unknown outcome'};
assert.strictEqual(ui.dashboardWatch(unknown,{id:'b'.repeat(32),itemId:id,phase:'stopped',active:false,message:''}),unknown);
assert.strictEqual(ui.dashboardWatch(unknown,{id:'a'.repeat(32),itemId:id,phase:'running',active:true,message:'Running'}).id,'a'.repeat(32));
''')

    def test_ready_pc_and_console_entries_show_only_play(self):
        self.behavior('''
const c=ui.dashboardLibrary(catalogFixture()),s={backend:'native',dashboard:c,games:[],jobs:[],stream:{}};const rows=ui.libraryGames(s);
const pc=ui.actions(rows[0],s,null),local=ui.actions(rows[1],s,null);
assert(pc.canPlay&&pc.showPlay&&!pc.canPrepare&&!pc.showPrepare);
assert(local.canPlay&&local.showPlay&&!local.canPrepare&&!local.showPrepare);assert.strictEqual(local.playRoute,'/api/dashboard/action');assert.strictEqual(local.badge,'Ready on PS5');
for(const changed of [{...s,dashboardWatch:{phase:'unknown',message:'Unknown outcome'}},{...s,dashboard:{...c,operation:{...c.operation,active:true,message:'Wait'}}}]) {
 for(const row of rows){const a=ui.actions(row,changed,null);assert(!a.canPlay&&!a.canPrepare);assert(a.playReason);}
}
''')

    def test_combined_pc_close_still_requires_exact_owned_source_and_session(self):
        self.behavior('''
const c=ui.dashboardLibrary(catalogFixture()),s={backend:'native',dashboard:c,games:[],jobs:[],stream:{phase:'running',game:'PC/Game',runId:'2'.repeat(32),canClose:true,gamePid:77,dataCountersObserved:false}};
const rows=ui.libraryGames(s);let a=ui.actions(rows[0],s,null);assert(a.close&&a.canPlay&&!a.canPrepare);assert.strictEqual(a.playLabel,'Close game');assert.strictEqual(a.badge,'Launched');
a=ui.actions(rows[1],s,null);assert(!a.close&&!a.canPlay);assert(!ui.actions(rows[0],s,{name:rows[0].name}).canPlay);
assert.strictEqual(ui.playBody(s,rows[0].name,true),'2'.repeat(32));
''')

    def test_waiting_and_uncertain_operation_panels_never_claim_gameplay(self):
        self.behavior('''
const c=ui.dashboardLibrary(catalogFixture());c.operation={id:'a'.repeat(32),itemId:c.items[1].id,phase:'waiting-for-dashboard-exit',active:true,message:'Waiting for native exit.'};
let panel=ui.sessionPanel({dashboard:c,games:[],stream:{}},null);assert(panel.visible&&!panel.primaryVisible);assert(panel.heading.includes('Closing dashboard'));assert(panel.ownershipLabel.includes('exit normally'));
c.operation.phase='unknown';panel=ui.sessionPanel({dashboard:c,games:[],stream:{}},null);assert(panel.failed&&!panel.primaryVisible);assert(panel.heading.includes('unknown'));
assert(!JSON.stringify(panel).includes('gameplay confirmed'));
''')

    def test_native_page_never_falls_back_to_pc_actions_when_combined_library_is_missing(self):
        self.behavior('''
assert.strictEqual(ui.libraryGames({backend:'native',dashboardRequired:true,games:[game]}).length,0);
assert.strictEqual(ui.libraryGames({backend:'native',games:[game]}).length,1);
const s={backend:'native',dashboardRequired:true,dashboardUnavailable:'Service unavailable',stream:{},jobs:[],setup:{},library:{phase:'ready'}};
assert(!ui.canRefresh(s,null));assert(ui.dashboardBusy(s));
''')

    def test_dashboard_name_and_library_scope_are_shared_in_the_embedded_page(self):
        self.assertIn('<title>Blackwire</title>', PAGE)
        self.assertIn('alt="Blackwire logo">Blackwire</div>', PAGE)
        self.assertIn('Your library</h2>', PAGE)

    def test_ready_sources_hide_send_even_when_cached_capabilities_allow_it(self):
        self.behavior('''
const source={name:'Prepared source',startupReady:true,prepared:true,canPrepare:true,canPlay:true,canStart:true};
const fixtureIdle={backend:'native',stream:{},jobs:[]};
let a=ui.actions(source,fixtureIdle,null);assert(a.showPlay&&a.canPlay&&!a.showPrepare&&!a.canPrepare);
for(const state of [{...fixtureIdle,jobs:[{name:source.name,state:'running'}]},{...fixtureIdle,stream:{phase:'running',game:'Another source'}}]) {
 a=ui.actions(source,state,null);assert(!a.canPrepare&&!a.showPrepare);assert(a.showPlay&&!a.canPlay&&a.playReason);
}
assert(!ui.actions(source,fixtureIdle,{name:source.name}).canPrepare);
''')

    def test_unready_pc_shows_only_send_and_does_not_invent_available_actions(self):
        self.behavior('''
const raw=catalogFixture();raw.items[0].startupReady=false;raw.items[0].canPlay=true;raw.items[0].canSend=true;
const s={backend:'native',dashboard:ui.dashboardLibrary(raw),games:[],jobs:[],stream:{}};
const row=ui.libraryGames(s)[0];let a=ui.actions(row,s,null);
assert(a.showPrepare&&a.canPrepare&&!a.showPlay&&!a.canPlay);
assert.throws(()=>ui.dashboardBody(s,row,'play'),/available action/);
assert.strictEqual(ui.dashboardBody(s,row,'send').action,'send');
row.canPrepare=false;row.preparationReason='This source is unsupported.';
a=ui.actions(row,s,null);assert(a.showPrepare&&!a.canPrepare&&!a.showPlay&&!a.canPlay);assert.strictEqual(a.prepareReason,row.preparationReason);
a=ui.actions(row,s,{name:row.name});assert(!a.canPrepare&&a.prepareReason.includes('Wait'));
raw.items[0].startupReady=true;raw.items[0].canSend=true;
const changed={...s,dashboard:ui.dashboardLibrary(raw)},ready=ui.libraryGames(changed)[0];
assert.throws(()=>ui.dashboardBody(changed,ready,'send'),/available action/);
''')

    def test_standard_controller_navigation_repeats_but_confirmation_does_not(self):
        self.behavior('''
const pad={mapping:'standard',connected:true,buttons:Array.from({length:16},()=>({pressed:false,value:0})),axes:[0,0]};
pad.buttons[15].pressed=true;
let p=ui.controllerStep(null,pad,1000);assert.strictEqual(p.action,'ArrowRight');
p=ui.controllerStep(p,pad,1100);assert.strictEqual(p.action,'');
p=ui.controllerStep(p,pad,1440);assert.strictEqual(p.action,'ArrowRight');
p=ui.controllerStep(p,pad,1500);assert.strictEqual(p.action,'');
pad.buttons[15].pressed=false;pad.buttons[0].pressed=true;
p=ui.controllerStep(p,pad,1600);assert.strictEqual(p.action,'Confirm');
for(const now of [2100,2600,5000]) {p=ui.controllerStep(p,pad,now);assert.strictEqual(p.action,'');}
pad.buttons[0].pressed=false;p=ui.controllerStep(p,pad,5100);assert.strictEqual(p.action,'');
pad.buttons[0].pressed=true;p=ui.controllerStep(p,pad,5200);assert.strictEqual(p.action,'Confirm');
pad.buttons[0].pressed=false;pad.axes[0]=-.8;assert.strictEqual(ui.controllerStep(null,pad,6000).action,'ArrowLeft');
''')

    def test_unmapped_absent_or_invalid_controller_data_never_sends_an_action(self):
        self.behavior('''
for(const pad of [null,{}, {mapping:'',buttons:Array(16).fill({pressed:true})},{mapping:'standard',connected:false,buttons:Array(16).fill({pressed:true})},{mapping:'standard',buttons:[{pressed:true}]}])
 assert.strictEqual(ui.controllerStep(null,pad,1000).action,'');
const pad={mapping:'standard',buttons:Array(16).fill({pressed:true})};
for(const now of [NaN,Infinity,-1,'1000'])assert.strictEqual(ui.controllerStep(null,pad,now).action,'');
for(const axis of [NaN,Infinity,-Infinity])assert.strictEqual(ui.controllerStep(null,{mapping:'standard',buttons:Array(16).fill({pressed:false}),axes:[axis,0]},1000).action,'');
''')

    def test_preload_launch_without_delivery_witness_keeps_counters_unknown(self):
        self.behavior('''
const message='Game launched; private reader inputs assigned. Delivery and the game screen are not yet verified.';
for(const value of [null,undefined,0]) {
 const stream={phase:'running',game:'Game',message,networkBytes:value,dataCountersObserved:false};
 assert.strictEqual(ui.sessionNotice(stream,()=>{throw Error('Unknown counter formatted');}),message);
 const badge=ui.actions({name:'Game'},{backend:'native',stream,jobs:[]},null).badge;
 assert.strictEqual(badge,'Launched');
}
assert(ui.sessionNotice({phase:'running',networkBytes:null},()=>{throw Error('Null formatted');}).includes('not been observed'));
assert(ui.sessionNotice({phase:'running',networkBytes:123,dataCountersObserved:true},n=>n+' B').includes('123 B read from PC'));
assert.strictEqual(ui.sessionNotice({phase:'launching',message:'Starting owned game'},()=>''),'Starting owned game');
''')

    def test_supported_image_uses_the_same_console_owned_actions(self):
        self.behavior('''
const name='Compressed/Preserved & Game.ffpfsc';
const g={name,title:'Preserved Game',titleId:'PPSA99999',sourceKind:'container',sourceBinding:'PPSA99999_'+'a'.repeat(64),canPrepare:true,canStart:true,canPlay:false,startupReady:false};
const s={backend:'native',library:{phase:'ready',generation:7},setup:{configured:true,revision:3},games:[g],jobs:[],stream:{}};
const a=ui.actions(g,s,null);assert(a.canPrepare&&a.showPrepare);assert(!a.canPlay&&!a.showPlay);assert.strictEqual(a.playRoute,'/api/start');
assert.strictEqual(JSON.stringify(ui.preparationBody(s,name)),JSON.stringify({name,libraryGeneration:7,settingsRevision:3}));
assert.strictEqual(JSON.stringify(ui.playBody(s,name,false)),JSON.stringify({name,libraryGeneration:7,settingsRevision:3}));
assert.strictEqual(ui.counts([g],'').ready,0);
assert.strictEqual(ui.counts([{...g,startupReady:true,canStart:false}],'').ready,1);
''')

    def test_unsupported_image_stays_unavailable_without_a_different_ui_path(self):
        self.behavior('''
const g={name:'Compressed/unsupported.ffpfsc',sourceKind:'container',status:'container_unverified',canPrepare:false,canStart:false,canPlay:false,startupReady:false,preparationReason:'This image layout is not supported.',playReason:'This image layout is not supported.'};
const s={backend:'native',library:{phase:'ready',generation:7},setup:{configured:true,revision:3},games:[g],jobs:[],stream:{}};
const a=ui.actions(g,s,null);assert(!a.canPrepare);assert(!a.canPlay);assert(a.prepareReason.includes('not supported'));
assert(ui.matches(g,'unsupported','not-ready'));assert(!ui.matches(g,'unsupported','ready'));
assert.strictEqual(ui.counts([g],'').ready,0);
''')

    def test_bound_dashboard_entry_submits_exact_matching_source_once(self):
        self.behavior('''
const binding='PPSA99999_'+'a'.repeat(64);
const s={backend:'native',library:{phase:'ready',generation:7},setup:{configured:true,revision:3},settings:{server:'pc',share:'Games'},games:[{name:'Collection/Café & Game',titleId:'PPSA99999',sourceBinding:binding,canStart:true}],jobs:[],stream:{}};
const f=ui.dashboardLaunch('#stream=Collection%2FCaf%C3%A9%20%26%20Game&binding='+encodeURIComponent(binding));
const a=f.step(s);assert.strictEqual(a.route,'/api/start');assert.strictEqual(a.body.name,'Collection/Café & Game');
assert.strictEqual(JSON.stringify(a.body),JSON.stringify({name:'Collection/Café & Game',libraryGeneration:7,settingsRevision:3,sourceBinding:binding}));
assert.strictEqual(f.step(s),null);f.accepted({ok:true,runId:'11111111111111111111111111111111'});assert.strictEqual(f.step(s),null);
''')

    def test_two_shares_keep_one_account_and_can_remove_the_optional_share(self):
        self.behavior('''
const s={backend:'native',setup:{editable:true,hasSavedCredentials:true,revision:3},settings:{server:'pc',share:'CompressedGames',share2:'FolderGames',user:'project-fixture'}};
const fields={server:'pc',share:'CompressedGames',share2:' FolderGames ',user:'project-fixture',keep:true};
const body=ui.setupBody(s,fields);assert.strictEqual(body.share2,'FolderGames');assert(body.keepCredentials);assert(!Object.hasOwn(body,'smbPassword'));
assert.strictEqual(ui.setupBody(s,{...fields,share2:''}).share2,'');
assert.throws(()=>ui.setupBody(s,{...fields,share2:'compressedgames'}),/two different/);
assert(!Object.hasOwn(ui.setupBody(s,{server:'pc',share:'CompressedGames',user:'project-fixture',keep:true}),'share2'));
''')

    def test_same_relative_path_in_two_shares_uses_the_selected_binding(self):
        self.behavior('''
const a='PPSA90001_'+'a'.repeat(64),b='PPSA90001_'+'b'.repeat(64),c=catalogFixture();
c.items=[c.items[0],{...c.items[0],id:'pc:PPSA90001:'+'b'.repeat(64)}];
const s={backend:'native',dashboard:c,library:{phase:'ready',generation:7},setup:{configured:true,revision:3},settings:{server:'pc',share:'CompressedGames',share2:'FolderGames'},jobs:[],stream:{},games:[
 {name:'PC/Game',titleId:'PPSA90001',sourceBinding:a,sourceShare:'CompressedGames',sourceKind:'container',canStart:true,dashboardLaunchReady:true},
 {name:'PC/Game',titleId:'PPSA90001',sourceBinding:b,sourceShare:'FolderGames',sourceKind:'folder',canStart:true,dashboardLaunchReady:false}]};
const rows=ui.libraryGames(s);assert.strictEqual(rows[0].sourceShare,'CompressedGames');assert.strictEqual(rows[0].sourceKind,'container');assert.strictEqual(rows[1].sourceShare,'FolderGames');assert.strictEqual(rows[1].sourceKind,'folder');assert.strictEqual(rows[1].dashboardLaunchReady,false);
const launch=ui.dashboardLaunch('#stream=PC%2FGame&binding='+b),request=launch.step(s);assert.strictEqual(request.route,'/api/start');assert.strictEqual(request.body.sourceBinding,b);assert.strictEqual(launch.step(s),null);
const ambiguous=ui.dashboardLaunch('#stream=PC%2FGame');assert.strictEqual(ambiguous.step(s),null);assert(ambiguous.error);
''')

    def test_bound_dashboard_entry_rejects_stale_or_missing_binding_before_mutation(self):
        self.behavior('''
const binding='PPSA99999_'+'a'.repeat(64);
for (const observed of [undefined,'PPSA99999_'+'b'.repeat(64),'PPSA99998_'+'a'.repeat(64)]) {
 const s={backend:'native',library:{phase:'ready',generation:7},setup:{configured:true,revision:3},settings:{server:'other',share:'Other'},games:[{name:'Game',titleId:'PPSA99999',sourceBinding:observed,canStart:true,canPlay:true,canPrepare:true}],jobs:[],stream:{}};
 const f=ui.dashboardLaunch('#stream=Game&binding='+binding);assert.strictEqual(f.step(s),null);assert(f.error);assert(f.message.includes('different PC source or connection'));assert.strictEqual(f.step(s),null);
}
''')

    def test_bound_dashboard_entry_checks_identity_before_existing_same_game_shortcut(self):
        self.behavior('''
const binding='PPSA99999_'+'a'.repeat(64);
const s={backend:'native',library:{phase:'ready',generation:7},setup:{configured:true,revision:3},settings:{},games:[{name:'Game',sourceBinding:'PPSA99999_'+'b'.repeat(64),canStart:false}],jobs:[],stream:{phase:'running',game:'Game'}};
const f=ui.dashboardLaunch('#stream=Game&binding='+binding);assert.strictEqual(f.step(s),null);assert(f.error);
''')

    def test_dashboard_fragment_rejects_ambiguous_or_malformed_binding_fields(self):
        self.behavior('''
const binding='PPSA99999_'+'a'.repeat(64);
for (const suffix of ['&binding=','&binding=%GG','&binding='+binding.toUpperCase(),'&binding=PPSA99999_short','&binding='+binding+'&binding='+binding,'&other='+binding,'&stream=Other','&binding='+binding+'&other=x']) assert.throws(()=>ui.dashboardLaunch('#stream=Game'+suffix),/invalid/);
assert(ui.dashboardLaunch('#stream=Game%26binding%3Dliteral&binding='+binding));
assert(ui.dashboardLaunch('#stream=Game%26literal'));
''')

    def test_bound_dashboard_entry_rechecks_identity_while_waiting_for_preparation(self):
        self.behavior('''
const binding='PPSA99999_'+'a'.repeat(64);
const s={backend:'native',library:{phase:'ready',generation:1},setup:{configured:true,revision:1},settings:{server:'pc',share:'Games'},games:[{name:'Game',titleId:'PPSA99999',sourceBinding:binding,canPrepare:true}],jobs:[],stream:{}};
const f=ui.dashboardLaunch('#stream=Game&binding='+binding);assert.strictEqual(f.step(s).route,'/api/send');f.accepted({ok:true,jobId:'11111111111111111111111111111111'});
s.games[0].sourceBinding='PPSA99999_'+'b'.repeat(64);s.games[0].prepared=true;s.games[0].canPlay=true;s.jobs=[{id:'11111111111111111111111111111111',name:'Game',state:'done'}];
assert.strictEqual(f.step(s),null);assert(f.error);
''')

    def test_ready_filter_is_stable_while_prepared_games_are_busy(self):
        self.behavior('''
const prepared={name:'Game',startupReady:true,canPlay:false,canStart:false,prepared:true};
assert(ui.matches(prepared,'','ready'));assert(!ui.matches(prepared,'','not-ready'));
assert.strictEqual(ui.counts([prepared,{name:'Other',startupReady:false}], '').ready,1);
const action=ui.actions(prepared,{backend:'native',stream:{phase:'running',game:'Other'},jobs:[]},null);
assert.strictEqual(action.badge,'Startup ready');assert(!action.canPlay);
''')

    def test_missing_startup_files_offer_send_before_play(self):
        self.behavior('''
const g={name:'Game',canStart:true,canPlay:false,canPrepare:true,startupReady:false};
const a=ui.actions(g,{backend:'native',stream:{},jobs:[]},null);
assert(a.canPrepare&&a.showPrepare&&!a.canPlay&&!a.showPlay);assert(!ui.matches(g,'','ready'));
''')

    def test_preparation_is_an_active_cancelable_console_session(self):
        self.behavior('''
const g={name:'Game',canStart:false};const s={backend:'native',stream:{phase:'preparing',game:'Game',runId:'11111111111111111111111111111111',canClose:true},jobs:[{name:'Game',state:'running'}]};
const a=ui.actions(g,s,null);assert(ui.active(s.stream));assert(a.close&&a.canPlay);assert.strictEqual(a.playLabel,'Cancel launch');assert.strictEqual(a.badge,'Preparing');
assert(!ui.actions({name:'Other',canStart:true},s,null).canPlay);
''')

    def test_failed_launch_is_not_labeled_as_successful_playback(self):
        self.behavior('''
const a=ui.actions({name:'Game',startupReady:true,canPlay:true},{backend:'native',stream:{game:'Game',phase:'error'},jobs:[]},null);
assert.strictEqual(a.badge,'Last launch failed');assert(a.canPlay);
''')

    def test_launch_link_submits_one_console_owned_start_and_never_polls_to_launch(self):
        self.behavior('''
const s={backend:'native',library:{phase:'ready',generation:7},setup:{configured:true,revision:3},settings:{server:'pc',share:'Games'},games:[{name:'Collection/Game',canStart:true,canPrepare:true}],jobs:[],stream:{}};
const f=ui.dashboardLaunch('#stream=Collection%2FGame');const a=f.step(s);
assert.strictEqual(a.route,'/api/start');f.accepted({ok:true,runId:'11111111111111111111111111111111'});
s.games[0].canPlay=true;assert.strictEqual(f.step(s),null);assert(!f.error);
''')
    def test_library_orders_active_and_ready_sources_first_without_merging_copies(self):
        self.behavior('''
const games=[{name:'Compressed/Alpha',title:'Alpha'},{name:'Second/Game',title:'Game',canPlay:true},{name:'First/Game',title:'Game',canPlay:true},{name:'Zulu',canPrepare:true}];
const state={games,stream:{phase:'running',game:'Zulu'}};
assert.strictEqual(JSON.stringify(ui.orderedGames(state).map(g=>g.name)),JSON.stringify(['Zulu','First/Game','Second/Game','Compressed/Alpha']));
assert.strictEqual(games[0].name,'Compressed/Alpha');assert.strictEqual(ui.orderedGames(state).length,4);
state.stream={};assert.strictEqual(ui.orderedGames(state)[0].name,'First/Game');
''')

    def test_dashboard_prepared_source_starts_once_with_current_revisions(self):
        self.behavior('''
const s={backend:'native',library:{phase:'ready',generation:7},setup:{configured:true,revision:3},settings:{server:'pc',share:'Games'},games:[{name:'Collection/Game',titleId:'PPSA99999',prepared:true,canPlay:true}],jobs:[],stream:{}};
const f=ui.dashboardLaunch('#stream=Collection%2FGame');let a=f.step(s,null);
assert.strictEqual(a.route,'/api/play');assert.strictEqual(JSON.stringify(a.body),JSON.stringify({name:'Collection/Game',libraryGeneration:7,settingsRevision:3}));
assert.strictEqual(f.step(s,null),null);f.accepted({ok:true,runId:'11111111111111111111111111111111'});assert.strictEqual(f.step(s,null),null);
''')

    def test_dashboard_preparation_waits_for_its_exact_successful_job(self):
        self.behavior('''
const s={backend:'native',library:{phase:'ready',generation:1},setup:{configured:true,revision:1},settings:{server:'pc',share:'Games'},games:[{source:'Collection/Game',titleId:'PPSA99999',canPrepare:true}],jobs:[],stream:{}};
const f=ui.dashboardLaunch('#stream=Collection%2FGame');assert.strictEqual(f.step(s).route,'/api/send');assert.strictEqual(f.step(s),null);
f.accepted({ok:true,jobId:'11111111111111111111111111111111'});
s.jobs=[{id:'11111111111111111111111111111111',name:'Collection/Game',state:'running'}];assert.strictEqual(f.step(s),null);
s.games[0].prepared=true;s.games[0].canPlay=true;assert.strictEqual(f.step(s),null);
s.jobs[0].state='done';let a=f.step(s);assert.strictEqual(a.route,'/api/play');assert.strictEqual(f.step(s),null);
''')

    def test_dashboard_unknown_mutation_response_is_never_replayed(self):
        self.behavior('''
const s={backend:'native',library:{phase:'ready',generation:1},setup:{configured:true,revision:1},settings:{},games:[{name:'Game',prepared:true,canPlay:true}],stream:{}};
let f=ui.dashboardLaunch('#stream=Game');assert(f.step(s));f.failed(new Error('Reply lost'));assert.strictEqual(f.step(s),null);assert(f.error);
f=ui.dashboardLaunch('#stream=Game');assert(f.step(s));assert.throws(()=>f.accepted({ok:true}),/could not be confirmed/);assert.strictEqual(f.step(s),null);
''')

    def test_dashboard_source_or_connection_change_blocks_automatic_play(self):
        self.behavior('''
for(const changed of ['title','revision','share','server']) {
 const s={backend:'native',library:{phase:'ready',generation:1},setup:{configured:true,revision:1},settings:{server:'pc',share:'Games'},games:[{name:'Game',titleId:'PPSA99999',canPrepare:true}],jobs:[],stream:{}};
 const f=ui.dashboardLaunch('#stream=Game');assert.strictEqual(f.step(s).route,'/api/send');f.accepted({ok:true,jobId:'11111111111111111111111111111111'});
 s.jobs=[{id:'11111111111111111111111111111111',name:'Game',state:'done'}];s.games[0].prepared=true;s.games[0].canPlay=true;
 if(changed==='title')s.games[0].titleId='PPSA99998';else if(changed==='revision')s.setup.revision++;else s.settings[changed]='different';
 assert.strictEqual(f.step(s),null);assert(f.error);assert(f.message.includes('changed'));
}
''')

    def test_dashboard_respects_other_active_game_and_existing_same_game(self):
        self.behavior('''
const s={backend:'native',library:{phase:'ready',generation:1},setup:{configured:true,revision:1},settings:{},games:[{name:'Game',canPlay:true}],stream:{phase:'running',game:'Other'}};
let f=ui.dashboardLaunch('#stream=Game');assert.strictEqual(f.step(s),null);assert(f.error);
s.stream.game='Game';f=ui.dashboardLaunch('#stream=Game');assert.strictEqual(f.step(s),null);assert(!f.error);
s.stream={};assert.strictEqual(f.step(s),null);
''')

    def test_dashboard_waits_for_setup_and_scan_but_rejects_ambiguous_source(self):
        self.behavior('''
const s={backend:'native',library:{phase:'scanning',generation:1},setup:{configured:false,revision:1},settings:{},games:[],stream:{}};
let f=ui.dashboardLaunch('#stream=Game');assert.strictEqual(f.step(s),null);s.setup.configured=true;assert.strictEqual(f.step(s),null);
s.library.phase='ready';assert.strictEqual(f.step(s),null);assert(f.error);
s.games=[{name:'Game',canPlay:true},{name:'Game',canPlay:true}];f=ui.dashboardLaunch('#stream=Game');assert.strictEqual(f.step(s),null);assert(f.error);
s.backend='pc';f=ui.dashboardLaunch('#stream=Game');assert.strictEqual(f.step(s),null);assert(f.error);
''')

    def test_dashboard_failed_missing_or_wrong_job_never_launches(self):
        self.behavior('''
for(const mode of ['error','missing','wrong-source']) {
 const s={backend:'native',library:{phase:'ready',generation:1},setup:{configured:true,revision:1},settings:{},games:[{name:'Game',canPrepare:true}],jobs:[],stream:{}};
 const f=ui.dashboardLaunch('#stream=Game');assert.strictEqual(f.step(s).route,'/api/send');f.accepted({ok:true,jobId:'11111111111111111111111111111111'});
 s.games[0].prepared=true;s.games[0].canPlay=true;
 s.jobs=mode==='missing'?[]:[{id:'11111111111111111111111111111111',name:mode==='wrong-source'?'Other':'Game',state:mode==='error'?'error':'done',message:'Preparation failed'}];
 for(let i=0;i<7;i++)assert.strictEqual(f.step(s),null);assert(f.error);
}
''')

    def test_dashboard_fragment_requires_one_safe_exact_source(self):
        self.behavior('''
assert.strictEqual(ui.dashboardLaunch(''),null);assert.strictEqual(ui.dashboardLaunch('#other=Game'),null);
for(const value of ['', '%GG','%00Game','%2FGame','Game%2F..%2FElse','Game%5CElse','Game%2F%2FElse'])assert.throws(()=>ui.dashboardLaunch('#stream='+value),/invalid/);
assert(ui.dashboardLaunch('#stream=Collection%2FCaf%C3%A9%20%26%20Game'));
''')

    def test_native_play_and_close_bind_the_current_source_and_exact_session(self):
        self.behavior('''
const state={backend:'native',library:{phase:'ready',generation:7,stale:false},setup:{revision:3},stream:{runId:'0123456789abcdef0123456789abcdef'}};
assert.strictEqual(JSON.stringify(ui.playBody(state,'Collection/Game',false)),JSON.stringify({name:'Collection/Game',libraryGeneration:7,settingsRevision:3}));
assert.strictEqual(ui.playBody(state,'Collection/Game',true),state.stream.runId);
state.stream.runId='00000000000000000000000000000000';assert.throws(()=>ui.playBody(state,'Game',true),/Refresh/);
assert.strictEqual(JSON.stringify(ui.playBody({backend:'pc',stream:{runId:'old'}},'Game',true)),JSON.stringify({name:'Game',runId:'old'}));
''')

    def test_native_launch_can_be_cancelled_and_close_is_not_duplicated(self):
        self.behavior('''
const nativeGame={name:'Collection/Game',canPlay:false,prepared:true};
const state={backend:'native',jobs:[],stream:{game:nativeGame.name,phase:'preflight',canClose:true,gamePid:0,readerPid:77}};
let action=ui.actions(nativeGame,state,null);assert(action.close&&action.canPlay);assert.strictEqual(action.playLabel,'Cancel launch');
state.stream.phase='closing';state.stream.canClose=false;action=ui.actions(nativeGame,state,null);
assert(!action.close&&!action.canPlay);assert.strictEqual(action.playLabel,'Closing…');assert(ui.active(state.stream));
state.stream.phase='stopped';assert(!ui.active(state.stream));
''')

    def test_native_preparation_sends_the_full_source_and_both_current_revisions(self):
        self.behavior('''
const state={backend:'native',library:{phase:'ready',generation:7,stale:false},setup:{revision:3}};
const body=ui.preparationBody(state,'Collection/Game');assert.strictEqual(JSON.stringify(body),JSON.stringify({name:'Collection/Game',libraryGeneration:7,settingsRevision:3}));
assert.strictEqual(state.library.generation,7); assert.strictEqual(state.setup.revision,3);
''')

    def test_native_preparation_rejects_incomplete_stale_or_unsafe_revision_state(self):
        self.behavior('''
for (const change of [{phase:'scanning'}, {stale:true},{generation:0},{generation:1.5},{generation:9007199254740992}]) {
  const state={backend:'native',library:Object.assign({phase:'ready',generation:1},change),setup:{revision:1}};
  assert.throws(()=>ui.preparationBody(state,'Collection/Game'),/Refresh/);
}
for (const revision of [0,1.5,4294967296]) assert.throws(()=>ui.preparationBody({backend:'native',library:{phase:'ready',generation:1},setup:{revision}},'Game'),/Refresh/);
''')

    def test_pc_preparation_keeps_the_existing_minimal_request(self):
        self.behavior("assert.strictEqual(JSON.stringify(ui.preparationBody({backend:'pc'},'Game')),JSON.stringify({name:'Game'}));")

    def test_prepared_native_files_do_not_count_as_verified_playback(self):
        self.behavior('''
const prepared={name:'Collection/Game',prepared:true,onConsole:true,canPrepare:true,canPlay:false};
const action=ui.actions(prepared,{backend:'native',stream:{phase:'unverified'},jobs:[]},null);
assert.strictEqual(action.badge,'Startup files prepared');assert(!action.canPlay);assert(action.canPrepare);
assert(!ui.matches(prepared,'','ready'));assert(ui.matches(prepared,'','not-ready'));
''')

    def behavior(self, assertions):
        harness = r'''
const fs = require('fs');
const vm = require('vm');
const assert = require('assert');
const context = {module:{exports:{}}};
vm.createContext(context);
vm.runInContext(JSON.parse(fs.readFileSync(0,'utf8')), context);
const ui = context.module.exports;
const game = {name:'Folder 42',title:'Mystery Game',titleId:'PPSA99999',canPrepare:true,canPlay:true};
const idle = {stream:{phase:'idle'},jobs:[]};
function catalogFixture(){
 const pc='pc:PPSA90001:'+ 'a'.repeat(64),local='folder:PPSA90001:'+'b'.repeat(64);
 return {schema:1,catalogGeneration:11,pcConnected:true,consoleConnected:true,items:[
  {id:pc,title:'PC Game',titleId:'PPSA90001',source:'PC/Game',kind:1,canPlay:true,canSend:false,startupReady:true,playReason:'',sendReason:'',cover:'/api/dashboard/cover?id='+pc},
  {id:local,title:'Console Game',titleId:'PPSA90001',source:'/data/homebrew/Games/Console Game',kind:2,canPlay:true,canSend:false,startupReady:true,playReason:'',sendReason:'Already on PS5.',cover:'/api/dashboard/cover?id='+local}],
  operation:{id:'',itemId:'',phase:'idle',message:'',active:false}};
}
'''
        result = subprocess.run(["node", "-e", harness + assertions], input=json.dumps(MODEL),
                                text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_search_matches_title_folder_and_title_id_without_title_gates(self):
        self.behavior("""
for (const query of ['mYsTeRy','folder 42','  ppsa99999  ']) assert(ui.matches(game,query));
assert(!ui.matches(game,'different'));
assert(ui.actions(game,idle,null).canPlay);
assert(!ui.actions(game,idle,null).canPrepare);
""")

    def test_native_connection_uses_pc_share_state_and_the_full_saved_unc_path(self):
        self.behavior(r'''
const link=ui.connection({backend:'native',settings:{server:'192.0.2.50',share:'PreservedGames'},setup:{configured:true},library:{phase:'ready'},ps5:{fileManager:false}});
assert(link.native); assert(link.configured); assert(link.online);
assert.strictEqual(link.folder,String.raw`\\192.0.2.50\PreservedGames`);
assert.strictEqual(link.label,'PC share connected');
''')

    def test_native_scanning_and_failed_refresh_cannot_report_a_connected_share(self):
        self.behavior('''
const state={backend:'native',settings:{server:'192.0.2.50',share:'PreservedGames'},library:{phase:'scanning'},ps5:{fileManager:true}};
assert(!ui.connection(state).online); assert.strictEqual(ui.connection(state).label,'Reading PC library');
state.library={phase:'error',stale:true}; assert(!ui.connection(state).online);
assert.strictEqual(ui.connection(state).label,'PC share needs attention');
''')

    def test_legacy_connection_fields_never_confirm_console_authority(self):
        self.behavior('''
for(const backend of [undefined,'pc','desktop','other']) {
 const state={backend,settings:{gamesFolder:'D:/PreservedGames',ps5Address:'192.0.2.20',server:'192.0.2.50',share:'Old'},setup:{configured:true},library:{phase:'ready'},ps5:{fileManager:true}};
 const link=ui.connection(state);assert(!link.native);assert(!link.configured);assert(!link.online);
 assert.strictEqual(link.folder,'');assert.strictEqual(link.label,'Console status unconfirmed');
}
''')

    def test_native_metadata_and_same_title_ids_do_not_enable_play_or_merge_sources(self):
        self.behavior('''
const a={...game,name:'One/Preserved Game',canPrepare:false,canPlay:false,preparationReason:'Preparation unavailable.',playReason:'Playback unavailable.'};
const b={...a,name:'Two/Preserved Game'};
const state={backend:'native',stream:{phase:'unverified'},jobs:[]};
assert(!ui.actions(a,state,null).canPlay); assert(!ui.actions(a,state,null).canPrepare);
assert.strictEqual(ui.counts([a,b],'').all,2); assert.strictEqual(ui.counts([a,b],'').ready,0);
''')

    def test_status_errors_show_the_backend_message(self):
        self.behavior('''
(async()=>{
const value=await ui.readState({ok:true,json:()=>Promise.resolve({backend:'native'})}); assert.strictEqual(value.backend,'native');
await assert.rejects(ui.readState({ok:false,json:()=>Promise.resolve({error:'Library status could not be serialized'})}),/Library status could not be serialized/);
})().catch(error=>{console.error(error);process.exit(1)});
''')

    def test_unreadable_status_has_a_clear_error(self):
        self.behavior('''
(async()=>{await assert.rejects(ui.readState({ok:true,json:()=>Promise.reject(new Error('parser details'))}),/unreadable status response/);
})().catch(error=>{console.error(error);process.exit(1)});
''')

    def test_native_setup_sends_explicit_new_credentials_and_the_form_revision(self):
        self.behavior('''
const state={backend:'native',settings:{server:'192.0.2.50'},setup:{editable:true,revision:7,hasSavedCredentials:false}};
const body=ui.setupBody(state,{server:' 192.0.2.75 ',share:' NewGames ',revision:6,user:' BlackwireTest ',password:'fixture'});
assert.strictEqual(body.server,'192.0.2.75');assert.strictEqual(body.share,'NewGames');assert.strictEqual(body.revision,6);
assert.strictEqual(body.smbUser,'BlackwireTest');assert.strictEqual(body.smbPassword,'fixture');assert.strictEqual(body.keepCredentials,false);
assert(!Object.hasOwn(body,'gamesFolder'));assert(!Object.hasOwn(body,'ps5Address'));
''')

    def test_native_setup_retains_sign_in_only_for_the_saved_pc(self):
        self.behavior('''
const state={backend:'native',settings:{server:'192.0.2.50'},setup:{editable:true,revision:1,hasSavedCredentials:true}};
const kept=ui.setupBody(state,{server:'192.0.2.50',share:'Other',keep:true});assert(kept.keepCredentials);
assert(!Object.hasOwn(kept,'smbUser'));assert(!Object.hasOwn(kept,'smbPassword'));
const moved=ui.setupBody(state,{server:'192.0.2.75',share:'Other',keep:true,user:'BlackwireTest',password:'fixture'});
assert.strictEqual(moved.keepCredentials,false);assert.strictEqual(moved.smbPassword,'fixture');
''')

    def test_native_setup_waits_for_its_own_operation_terminal_state(self):
        self.behavior('''
assert.strictEqual(ui.setupResult({operation:1,phase:'saved',busy:false},2),'pending');
assert.strictEqual(ui.setupResult({operation:2,phase:'saving',busy:true},2),'pending');
assert.strictEqual(ui.setupResult({operation:2,phase:'saved',busy:false},2),'saved');
assert.strictEqual(ui.setupResult({operation:2,phase:'unconfirmed',busy:false},2),'error');
assert.strictEqual(ui.setupResult({operation:2,phase:'error',busy:false},0),'pending');
''')

    def test_native_setup_keeps_the_captured_session_token_and_never_falls_back(self):
        self.behavior('''
(async()=>{
const calls=[];const body={server:'192.0.2.50',share:'Games',revision:1,keepCredentials:true};
const failure=Object.assign(new Error('old native app'),{status:404});
await assert.rejects(ui.saveSetup(body,(url,value,token)=>{calls.push(url);assert.strictEqual(token,'captured-session');return Promise.reject(failure)},'captured-session'),/old native app/);
assert.strictEqual(calls.length,1);assert.strictEqual(calls[0],'/api/setup');
})().catch(error=>{console.error(error);process.exit(1)});
''')

    def test_native_missing_config_does_not_infer_setup_from_stale_display_fields(self):
        self.behavior('''
const state={backend:'native',settings:{server:'192.0.2.50',share:'OldGames'},setup:{configured:false,busy:true},library:{phase:'scanning'}};
assert.strictEqual(ui.connection(state).configured,false);assert.strictEqual(ui.connection(state).label,'Checking PC connection');
''')

    def test_native_setup_unavailable_or_empty_source_cannot_be_submitted(self):
        self.behavior('''
assert.throws(()=>ui.setupBody({backend:'native',setup:{editable:false,message:'Wait for the connection'}},{}),/Wait for the connection/);
assert.throws(()=>ui.setupBody({backend:'native',setup:{editable:true}},{}),/PC address and shared folder/);
''')

    def test_setup_rejects_missing_and_legacy_backend_authority(self):
        self.behavior('''
for(const state of [null,{}, {backend:'pc'}, {backend:'desktop'}])
 assert.throws(()=>ui.setupBody(state,{server:'192.0.2.50',share:'Games',revision:3}),/console status/);
''')

    def test_readiness_tabs_use_actual_play_capability_and_combine_with_search(self):
        self.behavior("""
const unavailable = {...game,name:'Other Folder',canPlay:false};
assert(ui.matches(game,'mystery','ready'));
assert(!ui.matches(game,'mystery','not-ready'));
assert(ui.matches(unavailable,'other','not-ready'));
assert(!ui.matches(unavailable,'missing','not-ready'));
const counts = ui.counts([game,unavailable],'mystery');
assert.strictEqual(counts.all,2); assert.strictEqual(counts.ready,1); assert.strictEqual(counts['not-ready'],1);
const searched = ui.counts([game,unavailable],'other');
assert.strictEqual(searched.all,1); assert.strictEqual(searched.ready,0);
""")

    def test_unsupported_preparation_stays_disabled_with_server_reason(self):
        self.behavior("""
const unavailable = {...game,canPrepare:false,canPlay:false,preparationReason:'Format needs a supported reader.',playReason:'Prepare a supported source first.'};
const actions = ui.actions(unavailable,idle,null);
assert(!actions.canPrepare); assert(!actions.canPlay);
assert.strictEqual(actions.prepareReason,unavailable.preparationReason);
assert.strictEqual(actions.playReason,unavailable.playReason);
""")

    def test_legacy_ready_state_does_not_offer_a_send_repair_action(self):
        self.behavior("""
const legacy={...game,onConsole:true}; delete legacy.canPrepare;
assert(!ui.actions(legacy,idle,null).canPrepare);assert(!ui.actions(legacy,idle,null).showPrepare);assert(ui.actions(legacy,idle,null).canPlay);
assert(!ui.actions({...legacy,onConsole:false},idle,null).canPrepare);
const unavailable=ui.actions({...legacy,canPlay:false},idle,null);
assert(!unavailable.canPrepare); assert(unavailable.prepareReason.includes('Refresh Blackwire'));
""")

    def test_setup_failure_never_retries_or_falls_back_to_legacy_settings(self):
        self.behavior("""
(async()=>{
  for(const failure of [Object.assign(new Error('Not found'),{status:404}),new Error('Connection lost')]) {
    const routes=[];
    await assert.rejects(ui.saveSetup({server:'192.0.2.50',share:'Games',revision:3,keepCredentials:true},(route)=>{
      routes.push(route);return Promise.reject(failure);
    }),error=>error===failure);
    assert.deepStrictEqual(routes,['/api/setup']);
  }
})().catch(error=>{console.error(error);process.exitCode=1;});
""")

    def test_native_status_requires_typed_setup_and_a_current_capability_token(self):
        self.behavior('''
(async()=>{
const valid={backend:'native',settings:{server:'192.0.2.50',share:'Games'},games:[],setup:{supported:true,editable:true,busy:false,configured:true,hasSavedCredentials:true,revision:3,token:'f'.repeat(32)}};
const read=value=>ui.readAppState({ok:true,json:()=>Promise.resolve(value)});
assert.strictEqual(await read(valid),valid);
for(const mutate of [v=>delete v.backend,v=>v.backend='pc',v=>v.backend='desktop',v=>v.setup=null,v=>v.setup=[],v=>delete v.setup.configured,
 v=>v.setup.editable=1,v=>v.setup.busy='false',v=>v.setup.revision=0,v=>v.setup.revision=1.5,v=>v.setup.revision=NaN,v=>v.setup.revision=4294967296,
 v=>v.setup.token='',v=>v.setup.token='0'.repeat(32),v=>v.setup.token='F'.repeat(32),v=>v.settings.server=null,v=>v.games={},v=>v.setup.supported=false]) {
 const bad=JSON.parse(JSON.stringify(valid));mutate(bad);await assert.rejects(read(bad),/console status|connection settings/);
}
for(const bad of [null,[],{}, {backend:'native'}])await assert.rejects(read(bad),/console status/);
const unsupported=JSON.parse(JSON.stringify(valid));Object.assign(unsupported.setup,{supported:false,editable:false,token:'',revision:0});
assert.strictEqual(await read(unsupported),unsupported);assert(!ui.setupPresentation(unsupported,{fresh:true}).canSave);
const catalog=catalogFixture();assert.strictEqual(await ui.readState({ok:true,json:()=>Promise.resolve(catalog)}),catalog);
})().catch(error=>{console.error(error);process.exitCode=1;});
''')

    def test_setup_presentation_recomputes_connection_busy_and_stale_reasons(self):
        self.behavior('''
const state={backend:'native',setup:{supported:true,editable:true,busy:false,revision:3,token:'f'.repeat(32)}};
assert(!ui.setupPresentation(null,{}).canSave);
assert(ui.setupPresentation(null,{error:'Connection refused'}).reason.includes('Connection refused'));
assert(ui.setupPresentation(state,{fresh:true}).canSave);
state.setup.busy=true;state.setup.editable=false;state.setup.message='Checking the PC share.';
assert.strictEqual(ui.setupPresentation(state,{fresh:true}).reason,'Checking the PC share.');
state.setup.busy=false;state.setup.editable=true;assert(ui.setupPresentation(state,{fresh:true}).canSave);
for(const option of [{pending:{}},{posting:true},{operation:5},{dirty:true,revision:2,token:state.setup.token},{dirty:true,revision:3,token:'e'.repeat(32)}])
 assert(!ui.setupPresentation(state,{fresh:true,...option}).canSave);
assert(!ui.setupPresentation(state,{fresh:false,error:'Disconnected'}).canSave);
assert(!ui.setupPresentation({...state,dashboardUnavailable:'Library unavailable'},{fresh:true}).canSave);
assert(!ui.setupPresentation({...state,dashboardWatch:{phase:'unknown'}},{fresh:true}).canSave);
''')

    def test_setup_dom_failure_recovery_and_token_rotation_without_network(self):
        result = subprocess.run(["node", str(Path(__file__).with_name("setup_dom_harness.js")),
                                 str(ROOT / "desk" / "index.html")], text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_compatibility_reason_is_used_when_action_reason_is_missing(self):
        self.behavior("""
const unavailable = {...game,canPrepare:false,canPlay:false,compatibility:{state:'unavailable',reason:'This read method is not available.'}};
const actions=ui.actions(unavailable,idle,null);
assert.strictEqual(actions.prepareReason,unavailable.compatibility.reason);
assert.strictEqual(actions.playReason,unavailable.compatibility.reason);
""")

    def test_active_send_job_blocks_competing_actions_until_done(self):
        self.behavior("""
for (const state of ['queued','running']) {
  const actions=ui.actions(game,{...idle,jobs:[{name:'Another source',action:'send',state}]},null);
  assert(!actions.canPrepare); assert(!actions.canPlay); assert(actions.prepareReason.includes('Another source'));
}
for (const state of ['done','error']) {
  const actions=ui.actions(game,{...idle,jobs:[{name:game.name,action:'send',state}]},null);
  assert(!actions.canPrepare&&!actions.showPrepare); assert(actions.canPlay);
  const unready=ui.actions({...game,startupReady:false,canPlay:false},{...idle,jobs:[{name:game.name,action:'send',state}]},null);
  assert(unready.canPrepare&&unready.showPrepare&&!unready.canPlay&&!unready.showPlay);
}
""")

    def test_close_is_bound_to_matching_active_session_even_when_play_is_disabled(self):
        self.behavior("""
const state={jobs:[],stream:{phase:'running',game:game.name,gamePid:34,readerPid:33,runId:'test-run'}};
const actions=ui.actions({...game,canPlay:false},state,null);
assert(actions.close); assert(actions.canPlay); assert.strictEqual(actions.playLabel,'Close game'); assert(!actions.canPrepare);
assert.strictEqual(actions.badge,'Running');
const other=ui.actions({...game,name:'Another source'},state,null);
assert(!other.close); assert(!other.canPlay); assert(other.playReason.includes('Close'));
assert.strictEqual(other.badge,'Startup ready');
""")

    def test_unknown_session_and_pending_request_cannot_close_or_launch(self):
        self.behavior("""
const state={jobs:[],stream:{phase:'unknown',game:game.name,gamePid:34,readerPid:33}};
assert(!ui.actions(game,state,null).canPlay);
assert.strictEqual(ui.actions(game,state,null).badge,'Status unavailable');
const pending=ui.actions(game,idle,{name:game.name});
assert(!pending.canPrepare); assert(!pending.canPlay);
""")

    def test_all_inline_scripts_are_valid_javascript(self):
        scripts = "\n".join(re.findall(r"<script[^>]*>([\s\S]*?)</script>", PAGE))
        result = subprocess.run(["node", "--check", "-"], input=scripts, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_native_build_embeds_the_canonical_page_and_relays_common_actions(self):
        native = (ROOT / "library" / "main.c").read_text(encoding="utf-8")
        makefile = (ROOT / "library" / "Makefile").read_text(encoding="utf-8")
        self.assertIn('INCASSET(page_html, "../desk/index.html")', native)
        self.assertIn("main.c ../desk/index.html", makefile)
        for route in ("/api/send", "/api/play", "/api/close", "/api/settings", "/api/setup"):
            self.assertIn('strcmp(url, "' + route + '") == 0', native)
        self.assertNotRegex(PAGE, r"PPSA\d+|Resident Evil")


if __name__ == "__main__":
    unittest.main()
