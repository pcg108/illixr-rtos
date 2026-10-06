"""Reject mixed HPM definitions across application and plugin object libraries."""
import hashlib
from pathlib import Path
import re


def audit(ninja, enabled):
    path=Path(ninja);text=path.read_text();rows=[]
    for block in re.split(r'(?=^build )',text,flags=re.M):
        first=block.splitlines()[0] if block else ''
        if not first.startswith('build ') or '.obj:' not in first:continue
        application='CMakeFiles/app.dir/' in first
        plugin=bool(re.match(r'build plugins/[^/]+/CMakeFiles/.*plugin\.cpp\.obj:',first))
        if not application and not plugin:continue
        defines=re.search(r'^  DEFINES = (.*)$',block,re.M)
        values=re.findall(r'(?:^|\s)-DILLIXR_HPM_PROFILE=(\S+)',defines[1] if defines else '')
        if values!=[str(int(enabled))]:
            raise ValueError('Missing or inconsistent HPM definition: '+first)
        rows.append({'object':first.split(':',1)[0][6:],'hpm_enabled':enabled,'plugin':plugin})
    if not rows or not any(r['plugin'] for r in rows):
        raise ValueError('No application/plugin compile commands found for HPM audit')
    if enabled and not any('src/hpm.cpp.obj' in r['object'] for r in rows):
        raise ValueError('Enabled HPM build lacks profiler object')
    return {'passed':True,'enabled':enabled,'ninja_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'objects':rows}
