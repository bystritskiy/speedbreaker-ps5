#!/usr/bin/env python3
"""SpeedBreaker PS5: copy the app folder link.sh stages to the console over FTP
(ftpsrv, port 2121), as a folder title for ShadowMountPlus:
/data/homebrew/PPSA90505 by default.

    python3 -I port/ps5/tools/deploy-app.py --addr <console> [build/ps5/app/PPSA90505]

Each file goes up as NAME.part and is renamed once complete; sce_sys/param.json
goes last, so a first copy is not picked up half-written. Plain files are read
back and hashed (one already there with the same SHA-256 is kept). ftpsrv
decrypts a signed file (eboot.bin, sce_module/*.prx) when it is read, so those
are sent every time and only the server's reply vouches for them. Remote files
the folder doesn't have are listed, not deleted."""
import argparse
import ftplib
import hashlib
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[3]
# The development FSELF magics of eboot.bin and sce_module/libc.prx.
SELF_MAGICS = (bytes.fromhex('4f153d1d'), bytes.fromhex('5414f5ee'))


def remote_sha256(ftp, path):
    hashed = hashlib.sha256()
    try:
        ftp.retrbinary('RETR ' + path, hashed.update, blocksize=1 << 20)
    except ftplib.error_perm as error:
        if str(error).startswith('550'):
            return None
        raise
    return hashed.hexdigest()


def remove(ftp, path):
    try:
        ftp.delete(path)
    except ftplib.error_reply as error:
        # ftpsrv uses 226 for a successful DELE; ftplib expects 250.
        if not str(error).startswith('226'):
            raise
    except ftplib.error_perm as error:
        if not str(error).startswith('550'):
            raise


def listing(ftp, path, base=''):
    files, lines = set(), []
    try:
        ftp.retrlines('LIST ' + path, lines.append)
    except ftplib.error_perm:
        return files
    for line in lines:
        fields = line.split(None, 8)
        if len(fields) < 9 or fields[8] in ('.', '..'):
            continue
        name = base + fields[8]
        files |= listing(ftp, path + '/' + fields[8], name + '/') if line.startswith('d') else {name}
    return files


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--addr', required=True)
    p.add_argument('--dest', default='/data/homebrew/PPSA90505')
    p.add_argument('app', type=pathlib.Path, nargs='?', default=ROOT / 'build/ps5/app/PPSA90505')
    a = p.parse_args()
    if not (a.app / 'eboot.bin').is_file() or not (a.app / 'sce_sys/param.json').is_file():
        raise SystemExit('not an app folder (run port/ps5/tools/link.sh): ' + str(a.app))
    names = sorted((f.relative_to(a.app).as_posix() for f in a.app.rglob('*')
                    if f.is_file() and f.name != '.DS_Store'), key=lambda n: (n == 'sce_sys/param.json', n))
    ftp = ftplib.FTP()
    ftp.connect(a.addr, 2121, timeout=120)
    ftp.login()
    ftp.voidcmd('TYPE I')
    for name in sorted(listing(ftp, a.dest) - set(names)):
        print('only on console:', name)
    made = set()
    for name in names:
        local = a.app / name
        data = local.read_bytes()
        signed = data[:4] in SELF_MAGICS
        digest = hashlib.sha256(data).hexdigest()
        remote = a.dest + '/' + name
        if not signed and remote_sha256(ftp, remote) == digest:
            print('kept', name, flush=True)
            continue
        parent = ''
        for part in remote.strip('/').split('/')[:-1]:
            parent += '/' + part
            if parent not in made:
                try:
                    ftp.mkd(parent)
                except ftplib.error_perm:
                    pass
                made.add(parent)
        with local.open('rb') as source:
            ftp.storbinary('STOR ' + remote + '.part', source, blocksize=1 << 20)
        if not signed and remote_sha256(ftp, remote + '.part') != digest:
            raise SystemExit('readback failed, left as ' + remote + '.part')
        remove(ftp, remote)
        ftp.rename(remote + '.part', remote)
        print('sent', name, len(data), flush=True)
    ftp.quit()


if __name__ == '__main__':
    main()
