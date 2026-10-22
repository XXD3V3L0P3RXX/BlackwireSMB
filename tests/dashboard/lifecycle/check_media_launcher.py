"""Actual main.c Media publication checks; Sony calls are modeled, no console."""
# SPDX-License-Identifier: GPL-3.0-or-later
from pathlib import Path
import argparse,hashlib,json,os,shlex,subprocess,tempfile


def pin(path):
    data=path.read_bytes();return {'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();root=args.root.resolve();out=args.output.resolve()
    out.mkdir(parents=True,exist_ok=False)
    library=root/'src/blackwire/library';fixture=Path(__file__).with_name('test_media_launcher.c')
    inputs=[Path(__file__),fixture,library/'main.c',library/'param.json',library/'icon0.png',library/'source_param.c']
    before={str(p):pin(p) for p in inputs};commands=[]
    def run(label,argv):
        argv=list(map(str,argv))
        result=subprocess.run(argv,cwd=library,capture_output=True,text=True,timeout=180,
            env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1:abort_on_error=1',
                 'UBSAN_OPTIONS':'halt_on_error=1:print_stacktrace=1'})
        commands.append({'label':label,'argv':argv,'exitCode':result.returncode})
        (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        (out/(label+'.log')).write_text(result.stdout+result.stderr)
        if result.returncode:raise RuntimeError(label+' failed; partial output retained')
        return result.stdout
    includes=[library,root/'src/blackwire/streamreader',root/'src/blackwire/dashboard/service',
        root/'src/blackwire/dashboard/model',root/'src/blackwire/dashboard/local-removal',
        root/'vendor/libsmb2/include',root/'vendor/sqlite']
    flags=['-std=gnu11','-D_GNU_SOURCE','-Wall','-Wextra','-Werror',
        '-Wno-unused-function','-Wno-unused-variable','-ffunction-sections','-fdata-sections',
        '-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer']
    flags += [str(x) for directory in includes for x in ['-I',directory]]
    with tempfile.TemporaryDirectory(prefix='blackwire-media-host-') as temporary:
        parent=Path(temporary)/'apps';parent.mkdir()
        define='-DBW_MEDIA_APP_PARENT='+json.dumps(str(parent))
        objects=[];dependencies=set()
        for index,source in enumerate((fixture,library/'source_param.c')):
            target=out/(str(index)+'.o');dep=out/(str(index)+'.d')
            run('compile-'+str(index),['clang-18',*flags,define,'-MMD','-MF',dep,'-c',source,'-o',target])
            objects.append(target)
            body=dep.read_text().replace('\\\n',' ').split(':',1)[1]
            dependencies.update((Path(n) if Path(n).is_absolute() else library/n).resolve() for n in shlex.split(body))
        binary=out/'media-launcher'
        run('link',['clang-18',*flags,*objects,'-Wl,--gc-sections',
            '-Wl,--wrap=write','-Wl,--wrap=close','-Wl,--wrap=rename','-o',binary])
        result=json.loads(run('actual-media-cases',[binary]))
    if not result['passed'] or before!={str(p):pin(p) for p in inputs}:raise ValueError('Source changed or cases failed')
    dependency_pins={str(p):pin(p) for p in sorted(dependencies)}
    proof={'passed':True,'format':'blackwire-ordinary-media-web-launcher-host/1','result':result,
        'source':before,'compilerDependencies':dependency_pins,'sourceBeforeAfterMatch':True,
        'sanitizers':['ASan','UBSan'],'modeledNormalSonyRegistration':True,
        'actualMainAndFilesystem':True,'titleId':'PSBW50001','titleName':'Blackwire',
        'applicationCategoryType':66048,'deeplinkUri':'http://127.0.0.1:5918/',
        'logo':pin(library/'icon0.png'),'commands':commands,'consoleContacted':False,
        'wholeElfBuilt':False,'runtimeMediaVisibilityProved':False}
    (out/'proof.json').write_text(json.dumps(proof,indent=2)+'\n')
    print(json.dumps({'passed':True,'result':result,'proof':pin(out/'proof.json')}))


if __name__=='__main__':main()
