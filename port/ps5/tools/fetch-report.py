#!/usr/bin/env python3
"""Fetch PS5 session logs, or request the running game's complete bug-report ZIP.

The game consumes /data/speedbreaker/.request-bug-report on its UI thread.
The report includes the current frame, settings and diagnostics; nothing on
the console needs to be touched. The app must be running.
"""
import argparse
import ftplib
import io
import pathlib
import time
import zipfile


def listing(ftp, path):
    rows=[]
    ftp.retrlines('LIST '+path,rows.append)
    return [(r[0]=='d',r.split(None,8)[8]) for r in rows if len(r.split(None,8))==9 and r.split(None,8)[8] not in ('.','..')]


def reports(ftp, root):
    return {name for directory,name in listing(ftp,root)
            if not directory and name.startswith('speedbreaker-bug-report-') and name.endswith('.zip')}


def download(ftp, remote, target):
    target.parent.mkdir(parents=True,exist_ok=True)
    partial=target.with_name(target.name+'.part')
    with partial.open('wb') as out: ftp.retrbinary('RETR '+remote,out.write,blocksize=1<<20)
    partial.replace(target)
    print('saved',target,flush=True)


def request_report(ftp,root,output,timeout=60):
    before=reports(ftp,root)
    target=root+'/.request-bug-report'
    ftp.storbinary('STOR '+target+'.part',io.BytesIO(b'FTP request\n'))
    ftp.rename(target+'.part',target)
    print('requested a report; waiting for the running app',flush=True)
    deadline=time.monotonic()+timeout
    while time.monotonic()<deadline:
        new=reports(ftp,root)-before
        if new:
            name=sorted(new)[-1];path=output/name
            download(ftp,root+'/'+name,path)
            with zipfile.ZipFile(path) as report:
                bad=report.testzip()
                if bad:raise ValueError('corrupt report member: '+bad)
                print('ZIP verified:',', '.join(report.namelist()),flush=True)
            return path
        time.sleep(1)
    raise TimeoutError('No completed report. Start the updated app and retry; ordinary logs were fetched separately.')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--addr',required=True)
    p.add_argument('--root',default='/data/speedbreaker')
    p.add_argument('--output',type=pathlib.Path,required=True)
    p.add_argument('--request-report',action='store_true')
    p.add_argument('--timeout',type=float,default=60)
    a=p.parse_args()
    with ftplib.FTP() as ftp:
        ftp.connect(a.addr,2121,timeout=30);ftp.login();ftp.voidcmd('TYPE I')
        # Fetch existing diagnostics even when the game is stopped/crashed.
        for folder in ('logs','crashes'):
            remote=a.root+'/'+folder
            try: entries=listing(ftp,remote)
            except ftplib.error_perm as e:
                if str(e).startswith('550'):continue
                raise
            for directory,name in entries:
                if not directory:download(ftp,remote+'/'+name,a.output/folder/name)
        if a.request_report:request_report(ftp,a.root,a.output,a.timeout)

if __name__=='__main__':main()
