#!/usr/bin/env python3
"""Qualify the actual opt-in startup-trace packaging guard without console I/O."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,sys
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[3]
def pin(path):
 raw=path.read_bytes();return {"bytes":len(raw),"sha256":hashlib.sha256(raw).hexdigest()}
def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument("--output",required=True,type=Path);ap.add_argument("--host-only",action="store_true");a=ap.parse_args();out=a.output.resolve()
 if not out.is_relative_to(ROOT/"build") or out.exists():raise ValueError("Use a fresh build output")
 out.mkdir(parents=True)
 inputs=[ROOT/"tools/build/app.py",ROOT/"tools/build/launch_timing.py",ROOT/"tools/build/app-recipe.json",Path(__file__)];before={str(p.relative_to(ROOT)):pin(p) for p in inputs}
 sys.path.insert(0,str(ROOT/"tools/build"));spec=importlib.util.spec_from_file_location("startup_trace_actual_app",ROOT/"tools/build/app.py");app=importlib.util.module_from_spec(spec);spec.loader.exec_module(app)
 checks=0;cases=[]
 def require(value):
  nonlocal checks
  checks+=1
  if not value:raise AssertionError("Startup trace build guard assertion failed")
 def refused(label,fn):
  try:fn()
  except ValueError:cases.append(label);return
  raise AssertionError("Guard accepted "+label)
 flags=list(app.STARTUP_TRACE_FLAGS);file_flags=["-DBW_STARTUP_TRACE=1","-DBW_STARTUP_TRACE_STDOUT_ONLY=0"];require(app.startup_trace_flags(False)==[]);require(app.startup_trace_flags(True)==flags);require(app.startup_trace_flags(True,stdout_only=False)==file_flags)
 for value in (None,0,1,"true","false",[]):refused("non-boolean-selection-"+repr(value),lambda value=value:app.startup_trace_flags(value))
 for value in (None,0,1,"true","false",[]):refused("non-boolean-stdout-selection-"+repr(value),lambda value=value:app.startup_trace_flags(True,stdout_only=value))
 markers=b"\x00".join(app.STARTUP_TRACE_MARKERS);artifact={"bytes":len(markers),"sha256":hashlib.sha256(markers).hexdigest()}
 actual=["prospero-clang","-c","main.c",*flags,"-o","main.o"]
 good=app.startup_trace_proof(True,actual,artifact,markers);require(good["enabled"] and good["stdoutOnly"] and all(good["markerPresence"].values()));require(good["consoleTraceFileWritesEnabled"] is False and good["consoleActions"]==0 and good["provesRuntimeExecution"] is False)
 file_actual=["prospero-clang","-c","main.c",*file_flags,"-o","main.o"]
 file_good=app.startup_trace_proof(True,file_actual,artifact,markers,stdout_only=False);require(file_good["enabled"] is True and file_good["stdoutOnly"] is False and file_good["consoleTraceFileWritesEnabled"] is True and file_good["privateStartupTraceAbsent"] is False and file_good["expectedFlags"]==file_flags)
 for flag in file_flags:
  refused("file-mode-missing-actual-flag-"+flag,lambda flag=flag:app.startup_trace_proof(True,[v for v in file_actual if v!=flag],artifact,markers,stdout_only=False))
  refused("file-mode-duplicated-actual-flag-"+flag,lambda flag=flag:app.startup_trace_proof(True,file_actual+[flag],artifact,markers,stdout_only=False))
 refused("file-mode-has-stdout-command",lambda:app.startup_trace_proof(True,actual,artifact,markers,stdout_only=False))
 refused("stdout-mode-has-file-command",lambda:app.startup_trace_proof(True,file_actual,artifact,markers))
 refused("both-build-trace-modes",lambda:app.build(None,None,None,None,None,None,startup_trace_stdout=True,startup_trace_file=True))
 refused("build-stdout-mode-not-bool",lambda:app.build(None,None,None,None,None,None,startup_trace_stdout=1))
 refused("build-file-mode-not-bool",lambda:app.build(None,None,None,None,None,None,startup_trace_file=1))
 refused("build-diagnostic-mode-not-bool",lambda:app.build(None,None,None,None,None,None,diagnostic=1))
 refused("stdout-trace-without-diagnostic-build",lambda:app.build(None,None,None,None,None,None,startup_trace_stdout=True))
 refused("file-trace-without-diagnostic-build",lambda:app.build(None,None,None,None,None,None,startup_trace_file=True))
 for flag in flags:
  refused("missing-actual-flag-"+flag,lambda flag=flag:app.startup_trace_proof(True,[v for v in actual if v!=flag],artifact,markers))
  refused("duplicated-actual-flag-"+flag,lambda flag=flag:app.startup_trace_proof(True,actual+[flag],artifact,markers))
 for marker in app.STARTUP_TRACE_MARKERS:refused("missing-packaged-marker-"+marker.decode(),lambda marker=marker:app.startup_trace_proof(True,actual,{"bytes":len(markers.replace(marker,b"")),"sha256":hashlib.sha256(markers.replace(marker,b"")).hexdigest()},markers.replace(marker,b"")))
 refused("ordinary-build-has-trace-flags",lambda:app.startup_trace_proof(False,actual,artifact,markers))
 for flag in flags:
  name=flag[2:].split("=",1)[0]
  for variant in (["-D"+name],["-D"+name+"=0"],["-D",name+"=1"],["-U"+name],["-U",name]):
   refused("ordinary-build-has-trace-macro-"+repr(variant),lambda variant=variant:app.startup_trace_proof(False,["clang",*variant],artifact,markers))
   refused("diagnostic-build-has-conflicting-macro-"+repr(variant),lambda variant=variant:app.startup_trace_proof(True,actual+variant,artifact,markers))
 for marker in app.STARTUP_TRACE_MARKERS:
  raw=b"ordinary ELF fixture\x00"+marker+b"\x00";raw_pin={"bytes":len(raw),"sha256":hashlib.sha256(raw).hexdigest()}
  refused("ordinary-build-has-packaged-marker-"+marker.decode(),lambda raw=raw,raw_pin=raw_pin:app.startup_trace_proof(False,["clang","-c","main.c"],raw_pin,raw))
 refused("artifact-size-mismatch",lambda:app.startup_trace_proof(True,actual,{**artifact,"bytes":len(markers)+1},markers))
 refused("artifact-sha256-mismatch",lambda:app.startup_trace_proof(True,actual,{**artifact,"sha256":"0"*64},markers))
 ordinary=app.startup_trace_proof(False,["clang","-c","main.c"],{"bytes":0,"sha256":hashlib.sha256(b"").hexdigest()},b"");require(ordinary["enabled"] is False and ordinary["stdoutOnly"] is False and ordinary["privateStartupTraceAbsent"] is True and not any(ordinary["markerPresence"].values()))
 # Exercise the actual recipe adapter and packaging guard. Marker payloads in
 # this unit are synthetic; actual final ELF bytes are checked by app.build.
 recipe=json.loads((ROOT/"tools/build/app-recipe.json").read_text())["commands"]
 snapshot=json.dumps(recipe,sort_keys=True)
 release_commands=app.launch_timing.commands(recipe)
 diagnostic_commands=app.launch_timing.commands(recipe,enabled=True)
 require(json.dumps(recipe,sort_keys=True)==snapshot)
 require(not any(row["label"]=="compile-launch-timing" or "-DBW_LAUNCH_TIMING=1" in row["argv"] for row in release_commands))
 require(any(row["label"]=="compile-launch-timing" for row in diagnostic_commands))
 for row in diagnostic_commands:
  if "-c" in row["argv"]:row["argv"].insert(1,"-DBW_SEND_DIAGNOSTIC=1")
 operational=b"\x00".join(app.OPERATIONAL_MARKERS)
 diagnostic_package=b"\x7fELF-model-fixture\x00"+b"\x00".join(app.DIAGNOSTIC_MARKERS)+b"\x00"+operational
 release_package=b"\x7fELF-model-without-private-diagnostics\x00"+operational
 def artifact(raw):return {"bytes":len(raw),"sha256":hashlib.sha256(raw).hexdigest()}
 diagnostic_good=app.diagnostic_build_proof(True,diagnostic_commands,artifact(diagnostic_package),diagnostic_package)
 release_good=app.diagnostic_build_proof(False,release_commands,artifact(release_package),release_package)
 require(diagnostic_good["enabled"] and diagnostic_good["timingObjectIncluded"] and all(diagnostic_good["markerPresence"].values()))
 require(release_good["privateInstrumentationAbsent"] and not release_good["timingObjectIncluded"] and not any(release_good["markerPresence"].values()))
 refused("ordinary-build-with-diagnostic-commands",lambda:app.diagnostic_build_proof(False,diagnostic_commands,artifact(release_package),release_package))
 refused("diagnostic-build-with-ordinary-commands",lambda:app.diagnostic_build_proof(True,release_commands,artifact(diagnostic_package),diagnostic_package))
 refused("diagnostic-build-no-commands",lambda:app.diagnostic_build_proof(True,[],artifact(diagnostic_package),diagnostic_package))
 refused("diagnostic-build-bad-artifact",lambda:app.diagnostic_build_proof(True,diagnostic_commands,{"bytes":0,"sha256":"0"*64},diagnostic_package))
 for marker in (*app.DIAGNOSTIC_MARKERS,*app.STARTUP_TRACE_MARKERS):
  raw=release_package+b"\x00"+marker
  refused("ordinary-build-private-instrumentation-"+marker.decode(),lambda raw=raw:app.diagnostic_build_proof(False,release_commands,artifact(raw),raw))
 for marker in app.DIAGNOSTIC_MARKERS:
  raw=diagnostic_package.replace(marker,b"")
  refused("diagnostic-build-missing-instrumentation-"+marker.decode(),lambda raw=raw:app.diagnostic_build_proof(True,diagnostic_commands,artifact(raw),raw))
 for marker in app.OPERATIONAL_MARKERS:
  for enabled,commands,raw in ((True,diagnostic_commands,diagnostic_package.replace(marker,b"")),(False,release_commands,release_package.replace(marker,b""))):
   refused("missing-operational-marker-"+str(enabled)+marker.decode(),lambda enabled=enabled,commands=commands,raw=raw:app.diagnostic_build_proof(enabled,commands,artifact(raw),raw))
 for index,row in enumerate(diagnostic_commands):
  if "-c" not in row["argv"]:continue
  changed=json.loads(json.dumps(diagnostic_commands));changed[index]["argv"].remove("-DBW_SEND_DIAGNOSTIC=1")
  refused("actual-unit-missing-send-diagnostic-"+row["label"],lambda changed=changed:app.diagnostic_build_proof(True,changed,artifact(diagnostic_package),diagnostic_package))
 require(before=={str(p.relative_to(ROOT)):pin(p) for p in inputs})
 proof={"passed":True,"cases":len(cases)+3,"checks":checks,"refusalCases":cases,"actualBuildGuardImported":True,"fixtureScope":"Packager guard inputs only; marker bytes are synthetic and do not claim a linked or executed ELF","sourceBeforeAfterMatch":True,"source":before,"consoleActions":0}
 (out/"proof.json").write_text(json.dumps(proof,indent=2)+"\n");print(json.dumps({"passed":True,"cases":proof["cases"],"checks":checks,"consoleActions":0}))
if __name__=="__main__":main()
