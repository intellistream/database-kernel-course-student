"""Fetch a pinned, unmodified official teaching scaffold into a NEW directory."""
import argparse, pathlib, subprocess

URL = 'https://gitlab.eduxiji.net/csc1/csc-db/db2026.git'
REV = '8a7372685400e504433074af123987be9e0619ab'

def main():
    p=argparse.ArgumentParser(); p.add_argument('destination',type=pathlib.Path); a=p.parse_args()
    dest=a.destination.expanduser().resolve()
    if dest.exists(): p.error('Destination exists; choose a new directory. No files changed.')
    subprocess.run(['git','clone',URL,str(dest)],check=True)
    subprocess.run(['git','-C',str(dest),'checkout','--detach',REV],check=True)
    actual=subprocess.check_output(['git','-C',str(dest),'rev-parse','HEAD'],text=True).strip()
    if actual!=REV: raise RuntimeError('Revision mismatch')
    print('Official scaffold:',dest,'\nRevision:',actual)

if __name__=='__main__':main()
