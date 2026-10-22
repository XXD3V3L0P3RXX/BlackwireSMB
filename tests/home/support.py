"""Host-only paths; creates fresh test output and never contacts a console."""
from pathlib import Path
import argparse,sys

def locations(component):
    here=Path(__file__).resolve().parent
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root',type=Path,default=here.parents[1])
    parser.add_argument('--candidate',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    if sys.platform!='linux':raise ValueError('Run with Linux or WSL Python3 and clang-18')
    base=args.root.resolve(strict=True);candidate=(args.candidate or base).resolve(strict=True)
    out=args.output.resolve()/component;out.mkdir(parents=True,exist_ok=False)
    return here,base,candidate/'src/blackwire',out
