#!/usr/bin/env python3
"""Verify the generated patch manifest against owned, extracted language files.
Usage: python3 tests/ps5_text_catalog_test.py /path/to/extracted-ui-resources
"""
import hashlib
import json
from pathlib import Path
import re
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]


def read_table(data):
    count,table,text=struct.unpack_from('<III',data,12)
    result={}
    for index in range(count):
        key,offset=struct.unpack_from('<II',data,table+8+index*8)
        begin=text+8+offset
        result[key]=(begin,bytes(data[begin:data.index(0,begin)]))
    return result


def fnv1a64(data):
    h=0xcbf29ce484222325
    for c in data:h=((h^c)*0x100000001b3)&0xffffffffffffffff
    return h


def rewrite(patch,original):
    # The manifest has no game text: the slot comes from the extracted file.
    at,size=patch['resource_offset'],patch['size']
    old=original[at:at+size]
    assert len(old)==size and old.endswith(b'\0') and '%016x'%fnv1a64(old)==patch['fnv1a64'],patch['key']
    new=bytearray();cursor=0
    for i,remove,insert in patch['edits']:
        assert i>=cursor and i+remove<=size-1
        new+=old[cursor:i]+bytes.fromhex(insert);cursor=i+remove
    new+=old[cursor:size-1]
    assert len(new)<size,patch['key']
    return old,bytes(new+bytes(size-len(new)))


def main(folder):
    manifest=json.loads((ROOT/'port/ps5/text/patches.json').read_text())
    labels=read_table((folder/'LANGUAGES_LABELS.BIN').read_bytes())
    verified=0
    for language,digest in manifest['sources'].items():
        original=(folder/f'LANGUAGES_{language}.BIN').read_bytes()
        assert hashlib.sha256(original).hexdigest()==digest
        updated=bytearray(original);intended={}
        for patch in manifest['patches']:
            if patch['resource']!='LANGUAGES\\'+language+'.BIN':continue
            old,new=rewrite(patch,original);at=patch['resource_offset']
            assert len(new)==len(old) and new.endswith(b'\0')
            # Formatting arguments are ABI, not copy: type/order must stay exact.
            fmt=rb'%(?:\d+\$)?[-+ #0]*\d*(?:\.\d+)?[hlLzjt]*[diuoxXfFeEgGaAcsp%]'
            assert re.findall(fmt,old)==re.findall(fmt,new),patch['key']
            updated[at:at+len(new)]=new
            intended[patch['key']]=new.split(b'\0')[0];verified+=1
        before,after=read_table(original),read_table(updated)
        assert len(original)==len(updated) and before.keys()==after.keys()
        # Check all 4520 strings, not only the modified slots: aliases/suffixes
        # sharing storage must not cause any unrelated text to change.
        for key,(offset,value) in after.items():
            label=labels[key][1].decode('ascii')
            assert offset==before[key][0]
            assert value==intended.get(label,before[key][1]),(language,label)
            assert not re.search(rb'xbox|gamertag',value,re.I),(language,label)
            if 'CONTROLLER' in label or 'PROMPT' in label or label=='XENON_PROFILECHANGE' or label.startswith('QR_2P_'):
                assert not re.search(rb'\bSTART\b',value),(language,label)
        print(language+': layout and unrelated strings intact; no targeted platform terms')
    for resource,container in manifest['containers'].items():
        original=(folder/resource.replace('\\','_')).read_bytes()
        assert hashlib.sha256(original).hexdigest()==container['sha256']
        updated=bytearray(original)
        for patch in manifest['patches']:
            if patch['resource']!=resource:continue
            at=patch['resource_offset'];old,new=rewrite(patch,original)
            updated[at:at+len(new)]=new;verified+=1
        for copy in container['copies']:
            language=copy['language'];start,size=copy['offset'],copy['size']
            expected=bytearray((folder/f'LANGUAGES_{language}.BIN').read_bytes())
            for patch in manifest['patches']:
                if patch['resource']!='LANGUAGES\\'+language+'.BIN':continue
                at=patch['resource_offset'];new=rewrite(patch,bytes(expected))[1];expected[at:at+len(new)]=new
            assert updated[start:start+size]==expected,(resource,language,'boot cache differs from patched language')
        print(resource+': audited embedded language copies match standalone patches')
    print(verified,'patches verified across',len(manifest['sources']),'languages')

if __name__=='__main__':main(Path(sys.argv[1]))
