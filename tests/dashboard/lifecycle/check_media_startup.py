"""Actual bounded automatic Media coordinator; no platform or console calls."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,shlex

def pin(p):
    b=p.read_bytes();return {'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();root=args.root.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    source=Path(__file__).with_name('test_media_startup.c');header=root/'src/blackwire/library/media_startup.h'
    worker=source.with_name('test_media_startup_worker.c');library=root/'src/blackwire/library'
    files=[Path(__file__),source,header,worker,library/'main.c',library/'source_param.c',library/'param.json',library/'icon0.png',root/'src/blackwire/desk/index.html']
    before={str(p):pin(p) for p in files};commands=[]
    flags=['-std=c11','-Wall','-Wextra','-Werror','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer']
    env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1:abort_on_error=1','UBSAN_OPTIONS':'halt_on_error=1:print_stacktrace=1'}
    def run(label,argv,cwd=root):
        argv=list(map(str,argv));v=subprocess.run(argv,capture_output=True,text=True,timeout=30,env=env,cwd=cwd)
        commands.append({'label':label,'argv':argv,'exitCode':v.returncode,'stdout':v.stdout,'stderr':v.stderr})
        (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        if v.returncode:raise RuntimeError(label+' failed; evidence retained')
        return v.stdout
    binary=out/'media-startup'
    run('compile',['clang-18',*flags,source,'-o',binary])
    result=json.loads(run('actual-coordinator',[binary]))
    includes=[library,root/'src/blackwire/streamreader',root/'src/blackwire/dashboard/service',root/'src/blackwire/dashboard/model',
        root/'src/blackwire/dashboard/local-removal',root/'vendor/libsmb2/include',root/'vendor/sqlite']
    worker_flags=[*flags,'-D_GNU_SOURCE','-Wno-unused-function','-Wno-unused-variable','-Wno-misleading-indentation','-ffunction-sections','-fdata-sections']
    worker_flags += [str(x) for directory in includes for x in ['-I',directory]]
    binary=out/'media-startup-worker';objects=[];dependencies=set()
    for index,p in enumerate([worker,library/'source_param.c']):
        obj=out/(str(index)+'.o');dep=out/(str(index)+'.d')
        # main.c's incbin paths deliberately use the library working directory.
        argv=['clang-18',*worker_flags,'-MMD','-MF',dep,'-c',p,'-o',obj]
        run('worker-compile-'+str(index),argv,cwd=library)
        objects.append(obj)
        body=dep.read_text().replace('\\\n',' ').split(':',1)[1]
        dependencies.update((Path(n) if Path(n).is_absolute() else library/n).resolve() for n in shlex.split(body))
    run('worker-link',['clang-18',*worker_flags,*objects,'-pthread','-Wl,--gc-sections','-o',binary])
    worker_result=json.loads(run('actual-main-worker',[binary]))
    after={str(p):pin(p) for p in files}
    if not result['passed'] or not worker_result['passed'] or before!=after:raise RuntimeError('Coordinator failed or source changed')
    proof={'passed':True,'schema':'blackwire-media-startup-coordinator/1','result':result,'source':before,'sourceBeforeAfterMatch':True,
        'worker':worker_result,'compilerDependencies':{str(p):pin(p) for p in sorted(dependencies)},
        'commands':commands,'hostOnly':True,'consoleActions':0,'sdkCalls':0,'automaticMainWorkerHostProved':True,'platformAndBackendModeled':True}
    (out/'proof.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps({'coordinator':result,'worker':worker_result}))

if __name__=='__main__':main()
