#!/usr/bin/env python3
"""SpeedBreaker PS5: copy an extracted disc (default.xex, NFS/, Movies/) to the
console over FTP (ftpsrv, port 2121), where the game finds it:
/data/speedbreaker/game by default (install/locate.h: GetUserPath()/game).

    python3 -I port/ps5/tools/upload-game.py --addr <console> <extracted disc folder>

Checks the USA manifest before sending. Existing files are kept only if their
size and SHA-256 match; uploads are read back and verified before renaming.
Writes .nfsmw-install last, after every remote file matches the manifest."""
import argparse
import ftplib
import hashlib
import io
import pathlib
import re
import time


MARKER = '.nfsmw-install'


def load_manifest(path):
    text = path.read_text()
    info = re.search(r'MANIFEST_INFO\("([^"]+)",\s*0x[0-9A-Fa-f]+,\s*(\d+),\s*(\d+)ull\)', text)
    files = re.findall(r'MANIFEST_FILE\("([^"]+)",\s*(\d+)ull,\s*"([0-9a-f]{64})"\)', text)
    if not info or len(files) != int(info[2]) or sum(int(x[1]) for x in files) != int(info[3]):
        raise ValueError('invalid USA install manifest: ' + str(path))
    return info[1], [(name, int(size), digest) for name, size, digest in files]


def local_files(folder, entries):
    found = {p.relative_to(folder).as_posix().lower(): p for p in folder.rglob('*') if p.is_file()}
    result = {}
    for name, size, digest in entries:
        path = found.get(name.lower())
        if path is None or path.stat().st_size != size:
            raise ValueError('missing or wrong size: ' + name)
        with path.open('rb') as source:
            actual = hashlib.file_digest(source, 'sha256').hexdigest()
        if actual != digest:
            raise ValueError('unsupported or corrupt source file: ' + name)
        result[name] = path
    return result


def remote_matches(ftp, path, size, digest):
    try:
        if ftp.size(path) != size:
            return False
    except ftplib.error_perm as error:
        if str(error).startswith('550'):
            return False
        raise
    hashed = hashlib.sha256()
    ftp.retrbinary('RETR ' + path, hashed.update, blocksize=1 << 20)
    return hashed.hexdigest() == digest


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


def finish_install(ftp, destination, version, entries, repair=None):
    # Recheck every published file, including ones a caller skipped. No marker
    # may claim a verified install after a truncated or same-size corrupt copy.
    for name, size, digest in entries:
        if not remote_matches(ftp, destination + '/' + name, size, digest):
            if repair is None:
                raise ValueError('remote verification failed: ' + name)
            repair(name, size, digest)
            if not remote_matches(ftp, destination + '/' + name, size, digest):
                raise ValueError('remote repair verification failed: ' + name)
        print('verified', name, flush=True)
    xex = next(digest for name, _, digest in entries if name == 'default.xex')
    marker = ('# SpeedBreaker: all remote files verified against the USA manifest.\n'
              f'manifest_version = "{version}"\nxex_sha256 = "{xex}"\n'
              f'files = {len(entries)}\nbytes = {sum(size for _, size, _ in entries)}\n'
              'region = "usa"\n').encode()
    target = destination + '/' + MARKER
    ftp.storbinary('STOR ' + target + '.part', io.BytesIO(marker))
    if not remote_matches(ftp, target + '.part', len(marker), hashlib.sha256(marker).hexdigest()):
        raise ValueError('install marker readback failed')
    ftp.rename(target + '.part', target)
    print('published', MARKER, version, flush=True)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--addr', required=True)
    p.add_argument('--dest', default='/data/speedbreaker/game')
    p.add_argument('folder', type=pathlib.Path)
    a = p.parse_args()
    manifest = pathlib.Path(__file__).resolve().parents[3] / 'runtime/install/manifest.inc'
    version, entries = load_manifest(manifest)
    files = local_files(a.folder, entries)
    print('source verified:', len(entries), 'USA files', flush=True)
    ftp = ftplib.FTP()
    ftp.connect(a.addr, 2121, timeout=120)
    ftp.login()
    ftp.voidcmd('TYPE I')
    # A previous marker must not survive a partial update.
    remove(ftp, a.dest + '/' + MARKER)

    def mkdirs(path):
        cur = ''
        for part in path.strip('/').split('/'):
            cur += '/' + part
            try:
                ftp.mkd(cur)
            except ftplib.error_perm:
                pass

    start = time.time()
    total = 0
    for rel, size, digest in entries:
        f = files[rel]
        remote = a.dest + '/' + rel
        mkdirs(remote.rsplit('/', 1)[0])
        try:
            if ftp.size(remote) == size:
                print('kept', rel, flush=True)
                continue
        except ftplib.error_perm:
            pass
        with open(f, 'rb') as fh:
            ftp.storbinary('STOR ' + remote + '.part', fh, blocksize=1 << 20)
        if not remote_matches(ftp, remote + '.part', size, digest):
            raise ValueError('uploaded file readback failed: ' + rel)
        remove(ftp, remote)
        ftp.rename(remote + '.part', remote)
        total += size
        print(f'sent {rel} {size} ({total / max(time.time() - start, 0.001) / 1e6:.1f} MB/s)', flush=True)
    # Hash all remote files, also those kept by the cheap size check above.
    # On corruption, repair from the already verified source and check again.
    def repair(rel, size, digest):
        remote = a.dest + '/' + rel
        with files[rel].open('rb') as source:
            ftp.storbinary('STOR ' + remote + '.part', source, blocksize=1 << 20)
        if not remote_matches(ftp, remote + '.part', size, digest):
            raise ValueError('uploaded file readback failed: ' + rel)
        remove(ftp, remote)
        ftp.rename(remote + '.part', remote)
        print('repaired', rel, flush=True)
    finish_install(ftp, a.dest, version, entries, repair)
    ftp.quit()
    print(f'{len(files)} files, {total / 1e9:.2f} GB sent in {time.time() - start:.0f} s')


if __name__ == '__main__':
    main()
