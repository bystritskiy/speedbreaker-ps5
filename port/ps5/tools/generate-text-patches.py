#!/usr/bin/env python3
"""Generate scoped read-time text edits from the owned USA disc resources.

Input: extracted LANGUAGES/*.BIN (including LABELS.BIN) and NFS/ZDIR.BIN.
The original disc files remain unchanged. Output names each string slot by its
exact archive range, size and an FNV-1a hash of its original bytes, and the
edits that turn them into the new text. The game's own text is not stored:
the console reads it from the player's disc and checks the hash first. Inserted
bytes are only this port's copy (english.json, platform names, prompts).
No generic scan of game memory or files.
"""
import argparse
import difflib
import hashlib
import json
import pathlib
import re
import struct

ROOT=pathlib.Path(__file__).resolve().parents[3]
LANGUAGES=('ENGLISH','FRENCH','GERMAN','ITALIAN','SPANISH','DANISH','DUTCH','FINNISH','SWEDISH','POLISH')


def catalog(data):
    count=struct.unpack_from('<I',data,12)[0]
    table=struct.unpack_from('<I',data,16)[0]+8
    text=struct.unpack_from('<I',data,20)[0]+8
    result={}
    for index in range(count):
        key,offset=struct.unpack_from('<II',data,table+index*8)
        start=text+offset;end=data.index(0,start)
        result[key]=(start,data[start:end])
    return result


def ea_hash(name):
    h=0xffffffff
    for c in name.upper().encode():h=(h*33+c)&0xffffffff
    return h


def fnv1a64(data):
    h=0xcbf29ce484222325
    for c in data:h=((h^c)*0x100000001b3)&0xffffffffffffffff
    return h


def edits(old,new):
    # [at, remove, insert] over the slot's text, in order; the rest stays.
    result=[]
    for op,i1,i2,j1,j2 in difflib.SequenceMatcher(None,old,new,autojunk=False).get_opcodes():
        if op!='equal':result.append([i1,i2-i1,new[j1:j2]])
    rebuilt=bytearray();at=0
    for i,remove,insert in result:rebuilt+=old[at:i]+insert;at=i+remove
    if bytes(rebuilt+old[at:])!=new:raise ValueError('edit script does not rebuild the replacement')
    # An insert carries only new copy: a run long enough to be the game's own
    # wording means the alignment picked up original text.
    for _,_,insert in result:
        for size in range(12,len(insert)+1):
            if any(insert[k:k+size] in old for k in range(len(insert)-size+1)):
                raise ValueError(f'edit inserts original text: {insert!r}')
    return result


def tokens(data):
    return re.findall(rb'%(?:\d+\$)?[-+ #0]*\d*(?:\.\d+)?[hlLzjt]*[diuoxXfFeEgGaAcsp%]',data)


def localized(old,language,label):
    # Retain the original encoding and surrounding translation. Only platform
    # names are changed; button names use their physical DualSense spelling.
    value=old
    for pattern,new in [(rb'xbox\s*360',b'PS5'),(rb'xbox\s*live',b'Online'),
                        (rb'xbox\s*dashboard',b'Home'),(rb'xbox\s*communicator',b'Headset'),
                        (rb'xbox\s*guide',b'PS'),(rb'xbox',b'PS5'),(rb'gamertags?',b'ID')]:
        value=re.sub(pattern,new,value,flags=re.I)
    if label=='360_GUIDE':return b'PS'
    if label=='XENON_PROFILECHANGE' and b'START' in value:
        return {'GERMAN':b'Das aktive Spielerprofil wurde gewechselt. Zurueck zum Titelbildschirm.',
                'ITALIAN':b'Il profilo attivo e cambiato. Ritorno alla schermata del titolo.',
                'SPANISH':b'El perfil activo ha cambiado. Volviendo al titulo.'}.get(language,
                b'The active player profile changed. Returning to the title screen; unsaved progress may be lost.')
    if b'START' in value and ('PROMPT' in label or 'CONTROLLER' in label or label.startswith('QR_2P_')):
        value=value.replace(b'START',b'Options')
        if len(value)>len(old):
            prompts={'FRENCH':b'Appuyez sur Options','GERMAN':b'Options dr\xfccken','ITALIAN':b'Premi Options',
                     'SPANISH':b'Pulsa Options','DANISH':b'Tryk Options','DUTCH':b'Druk op Options',
                     'FINNISH':b'Paina Options','SWEDISH':b'Tryck Options','POLISH':b'Options'}
            value=prompts[language]
            # Keep every format parameter, even on a short reconnect prompt.
            params=tokens(old)
            if params:value=b'Pad '+b' / '.join(params)+b': Options'
            if len(value)>len(old):value=b'Options '+b' '.join(params)
    return value


def replacements(resource_dir,zdir):
    labels=catalog((resource_dir/'LANGUAGES_LABELS.BIN').read_bytes())
    english=json.loads((ROOT/'port/ps5/text/english.json').read_text())
    archive={r[0]:r for r in struct.iter_unpack('<6I',zdir.read_bytes())}
    patches=[];summary={};sources={};containers={};payloads={}
    for language in LANGUAGES:
        path=resource_dir/f'LANGUAGES_{language}.BIN';data=path.read_bytes();sources[language]=hashlib.sha256(data).hexdigest()
        payloads[language]=data
        entries=catalog(data);record=archive[ea_hash('LANGUAGES\\'+language+'.BIN')];count=0
        for key,(offset,old) in entries.items():
            label=labels[key][1].decode('ascii')
            if language=='ENGLISH':
                value=english.get(label)
                new=value.encode('cp1252') if value is not None else old
                if value is None and b'START' in old and ('PROMPT' in label or 'CONTROLLER' in label or label.startswith('QR_2P_')):
                    if label.startswith('QR_2P_'):new=b'Player '+b' / '.join(tokens(old))+b': Options'
                    elif tokens(old):new=b'Reconnect controller '+b' / '.join(tokens(old))+b'.^Press Options.'
                    else:new=b'Press Options to continue.' if len(old)>=25 else b'Press Options' if len(old)>=13 else b'Options'
            else:new=localized(old,language,label)
            if new==old:continue
            if len(new)>len(old):raise ValueError(f'{language} {label}: replacement too long ({len(new)} > {len(old)}): {new!r}')
            if tokens(new)!=tokens(old):raise ValueError(f'{language} {label}: format parameters changed: {old!r} -> {new!r}')
            patches.append(dict(archive=record[1],offset=(record[2]<<11)+offset,resource_offset=offset,
                                language=language,resource='LANGUAGES\\'+language+'.BIN',key=label,size=len(old)+1,
                                fnv1a64='%016x'%fnv1a64(old+b'\0'),edits=[[i,r,x.hex()] for i,r,x in edits(old,new)]))
            count+=1
        summary[language]=count
    # The boot reads English from GLOBALMEMORYFILE.BIN, a complete embedded
    # resource copy. Individual LANGUAGES files alone do not cover this path.
    standalone=list(patches)
    for container in ('GLOBAL_GLOBALMEMORYFILE.BIN','GLOBAL_INGAMEMEMORYFILE.BIN',
                      'GLOBAL_FRONTENDMEMORYFILE.BIN','GLOBAL_PERMANENTMEMORYFILE.BIN'):
        path=resource_dir/container
        if not path.exists():raise ValueError('missing boot-cache audit input: '+str(path))
        data=path.read_bytes();resource=container.replace('_','\\',1)
        record=archive[ea_hash(resource)];copies=[]
        for language,payload in payloads.items():
            begin=data.find(payload)
            if begin<0:continue
            if data.find(payload,begin+1)>=0:raise ValueError('ambiguous embedded resource: '+container+' '+language)
            copies.append(dict(language=language,offset=begin,size=len(payload)))
            for patch in standalone:
                if patch['language']!=language:continue
                extra=dict(patch,archive=record[1],offset=(record[2]<<11)+begin+patch['resource_offset'],
                           resource=resource,resource_offset=begin+patch['resource_offset'])
                patches.append(extra);summary[language]+=1
        containers[resource]=dict(sha256=hashlib.sha256(data).hexdigest(),copies=copies)
    patches.sort(key=lambda p:(p['archive'],p['offset']))
    for a,b in zip(patches,patches[1:]):
        if a['archive']==b['archive'] and a['offset']+a['size']>b['offset']:
            raise ValueError('overlapping string slots: '+a['key']+', '+b['key'])
    return patches,summary,sources,containers


def generate(patches,summary,sources,containers):
    doc=dict(source='USA language resources, exact ZDIR ranges',sources=sources,containers=containers,counts=summary,patches=patches)
    (ROOT/'port/ps5/text/patches.json').write_text(json.dumps(doc,indent=2)+'\n')
    def quoted(value):return '"'+''.join('\\x%02x'%c for c in value)+'"'
    table=[];index={}
    for p in patches:
        script=tuple((i,r,x) for i,r,x in p['edits'])
        if script not in index:index[script]=len(table);table.extend(script)
        p['_edit']=index[script]
    lines=['// Generated by generate-text-patches.py from the owned USA disc: ranges,',
           '// hashes of the original bytes and this port\'s edits; no game text.',
           '#pragma once','#include <cstdint>','#include <cstddef>','namespace ps5::text {',
           'struct Edit { uint16_t at, remove; const char* insert; uint16_t length; };',
           'struct Patch { uint32_t archive; uint64_t offset; uint32_t size; uint64_t hash; uint32_t edit, edits; };',
           'inline constexpr Edit kEdits[] = {']
    for i,r,x in table:
        lines.append(f'    {{ {i}, {r}, {quoted(bytes.fromhex(x))}, {len(x)//2} }},')
    lines.extend(['};','inline constexpr Patch kPatches[] = {'])
    for p in patches:
        lines.append(f'    {{ {p["archive"]}, {p["offset"]}ull, {p["size"]}, 0x{p["fnv1a64"]}ull, {p.pop("_edit")}, {len(p["edits"])} }}, // {p["language"]} {p["key"]}')
    lines.extend(['};','}'])
    (ROOT/'port/ps5/src/text_patches.h').write_text('\n'.join(lines)+'\n')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--resources',type=pathlib.Path,required=True)
    p.add_argument('--zdir',type=pathlib.Path,required=True)
    a=p.parse_args();patches,summary,sources,containers=replacements(a.resources,a.zdir);generate(patches,summary,sources,containers)
    print(json.dumps(summary),sum(summary.values()),'exact string patches')

if __name__=='__main__':main()
