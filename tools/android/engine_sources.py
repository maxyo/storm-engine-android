#!/usr/bin/env python3
"""Fetch exact open-source engine dependency revisions; no proprietary SDKs."""
import argparse
from pathlib import Path
import subprocess
SOURCES = {
    'spdlog': ('https://github.com/gabime/spdlog.git', 'eb3220622e73a4889eee355ffa37972b3cac3df5'),
    'fast_float': ('https://github.com/fastfloat/fast_float.git', 'b7f9d6ca3980de0f47251921f307e88c894f6daa'),
    'mimalloc': ('https://github.com/microsoft/mimalloc.git', 'f412df7a2b64421e1f1d61fde6055a6ea288e8f5'),
    'catch2': ('https://github.com/catchorg/Catch2.git', 'c4e3767e265808590986d5db6ca1b5532a7f3d13'),
}
p=argparse.ArgumentParser();p.add_argument('--directory',type=Path,required=True);a=p.parse_args()
for name,(url,revision) in SOURCES.items():
    target=a.directory.resolve()/name
    if not (target/'.git').exists():
        subprocess.run(['git','init','-q',str(target)],check=True)
        subprocess.run(['git','-C',str(target),'remote','add','origin',url],check=True)
    subprocess.run(['git','-C',str(target),'fetch','--depth','1','origin',revision],check=True)
    subprocess.run(['git','-C',str(target),'checkout','--detach','FETCH_HEAD'],check=True)
    print(name,subprocess.check_output(['git','-C',str(target),'rev-parse','HEAD'],text=True).strip(),flush=True)
