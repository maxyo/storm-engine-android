#!/usr/bin/env python3
"""Run production journal collection and activation with script/service boundaries recorded."""
from pathlib import Path
import os
import subprocess
import tempfile

here = Path(__file__).resolve().parent
root = here.parents[1]
nodes = root / 'src/libs/xinterface/src/nodes'

def member(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

titles = (nodes / 'xi_quest_titles.cpp').read_text()
texts = (nodes / 'xi_quest_texts.cpp').read_text()
with tempfile.TemporaryDirectory(prefix='storm-mobile-journal-') as temporary:
    build = Path(temporary)
    selected = [member(titles, 'void CXI_QUESTTITLE::RefreshMobileQuests('),
                member(titles, 'void CXI_QUESTTITLE::MobileCollect('),
                member(titles, 'bool CXI_QUESTTITLE::MobileApply('),
                member(texts, 'void CXI_QUESTTEXTS::StartQuestShow('),
                member(texts, 'void CXI_QUESTTEXTS::MobileCollect(')]
    (build / 'journal.inc').write_text('\n'.join(selected))
    binary = build / 'check'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-O1', '-g', '-D__ANDROID__',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-I'+str(root/'src/libs/xinterface/src'), '-I'+str(build),
                    str(here/'check.cpp'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
