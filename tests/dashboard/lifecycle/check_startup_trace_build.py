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
 inputs=[ROOT/"tools/build/app.py",Path(__file__)];before={str(p.relative_to(ROOT)):pin(p) for p in inputs}
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
 flags=list(app.STARTUP_TRACE_FLAGS);require(app.startup_trace_flags(False)==[]);require(app.startup_trace_flags(True)==flags)
 for value in (None,0,1,"true","false",[]):refused("non-boolean-selection-"+repr(value),lambda value=value:app.startup_trace_flags(value))
 markers=b"\x00".join(app.STARTUP_TRACE_MARKERS);artifact={"bytes":len(markers),"sha256":hashlib.sha256(markers).hexdigest()}
 actual=["prospero-clang","-c","main.c",*flags,"-o","main.o"]
 good=app.startup_trace_proof(True,actual,artifact,markers);require(good["enabled"] and good["stdoutOnly"] and all(good["markerPresence"].values()));require(good["consoleTraceFileWritesEnabled"] is False and good["consoleActions"]==0 and good["provesRuntimeExecution"] is False)
 for flag in flags:
  refused("missing-actual-flag-"+flag,lambda flag=flag:app.startup_trace_proof(True,[v for v in actual if v!=flag],artifact,markers))
  refused("duplicated-actual-flag-"+flag,lambda flag=flag:app.startup_trace_proof(True,actual+[flag],artifact,markers))
 for marker in app.STARTUP_TRACE_MARKERS:refused("missing-packaged-marker-"+marker.decode(),lambda marker=marker:app.startup_trace_proof(True,actual,{"bytes":len(markers.replace(marker,b"")),"sha256":hashlib.sha256(markers.replace(marker,b"")).hexdigest()},markers.replace(marker,b"")))
 refused("ordinary-build-has-trace-flags",lambda:app.startup_trace_proof(False,actual,artifact,markers))
 refused("artifact-size-mismatch",lambda:app.startup_trace_proof(True,actual,{**artifact,"bytes":len(markers)+1},markers))
 refused("artifact-sha256-mismatch",lambda:app.startup_trace_proof(True,actual,{**artifact,"sha256":"0"*64},markers))
 ordinary=app.startup_trace_proof(False,["clang","-c","main.c"],{"bytes":0,"sha256":hashlib.sha256(b"").hexdigest()},b"");require(ordinary["enabled"] is False and ordinary["stdoutOnly"] is False)
 require(before=={str(p.relative_to(ROOT)):pin(p) for p in inputs})
 proof={"passed":True,"cases":len(cases)+3,"checks":checks,"refusalCases":cases,"actualBuildGuardImported":True,"fixtureScope":"Packager guard inputs only; marker bytes are synthetic and do not claim a linked or executed ELF","sourceBeforeAfterMatch":True,"source":before,"consoleActions":0}
 (out/"proof.json").write_text(json.dumps(proof,indent=2)+"\n");print(json.dumps({"passed":True,"cases":proof["cases"],"checks":checks,"consoleActions":0}))
if __name__=="__main__":main()
