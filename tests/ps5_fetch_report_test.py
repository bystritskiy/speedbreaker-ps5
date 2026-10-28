#!/usr/bin/env python3
"""Report protocol: request publication, completed ZIPs, and validation."""
import importlib.util
import io
import pathlib
import tempfile
import unittest
import zipfile

spec=importlib.util.spec_from_file_location('fetch_report',pathlib.Path(__file__).resolve().parents[1]/'port/ps5/tools/fetch-report.py')
fetch=importlib.util.module_from_spec(spec);spec.loader.exec_module(fetch)

class FTP:
    def __init__(self,corrupt=False):
        output=io.BytesIO()
        with zipfile.ZipFile(output,'w') as z:z.writestr('summary.txt','PS5 diagnostics')
        self.payload=b'broken' if corrupt else output.getvalue()
        self.commands=[];self.published=False
    def retrlines(self,command,callback):
        callback('-rw-r--r-- 1 0 0 5 Oct 28 18:00 speedbreaker-bug-report-old.zip')
        callback('-rw-r--r-- 1 0 0 5 Oct 28 18:00 speedbreaker-bug-report-new.zip.partial')
        if self.published:callback('-rw-r--r-- 1 0 0 5 Oct 28 18:00 speedbreaker-bug-report-new.zip')
    def storbinary(self,command,source):self.commands.append(command);self.request=source.read()
    def rename(self,source,target):self.commands.append((source,target));self.published=True
    def retrbinary(self,command,callback,blocksize):callback(self.payload)

class Tests(unittest.TestCase):
    def test_publish_then_completed_zip(self):
        ftp=FTP()
        with tempfile.TemporaryDirectory() as folder:
            result=fetch.request_report(ftp,'/data/speedbreaker',pathlib.Path(folder),1)
            self.assertEqual(result.name,'speedbreaker-bug-report-new.zip')
            self.assertFalse(result.with_name(result.name+'.part').exists())
        self.assertEqual(ftp.commands,['STOR /data/speedbreaker/.request-bug-report.part',('/data/speedbreaker/.request-bug-report.part','/data/speedbreaker/.request-bug-report')])
    def test_invalid_zip_is_not_success(self):
        with tempfile.TemporaryDirectory() as folder, self.assertRaises(zipfile.BadZipFile):
            fetch.request_report(FTP(corrupt=True),'/data/speedbreaker',pathlib.Path(folder),1)
    def test_timeout_is_explicit(self):
        with tempfile.TemporaryDirectory() as folder, self.assertRaises(TimeoutError):
            fetch.request_report(FTP(),'/data/speedbreaker',pathlib.Path(folder),0)

if __name__=='__main__':unittest.main()
