#!/usr/bin/env python3

import os
import shutil
import subprocess
import textwrap
import time

if os.path.exists("stupid"):
  raise SystemExit("Error: stupid directory already exists")

t = {}
v = [10, 100, 1000, 8192, 16384, 32768, 65536, 131072, 262144, 524288, 1048576]
print("   size:rmtree   f-i check merge")
for i in v:
  start=time.time()
  if os.path.exists("stupid"):
    shutil.rmtree("stupid")
  t['rmtree'] = time.time()-start
  
  subprocess.call("git init --quiet stupid".split())

  start=time.time()
  p = subprocess.Popen("git fast-import --quiet".split(),
                       cwd="stupid", stdin=subprocess.PIPE, universal_newlines=True)
  p.stdin.write(textwrap.dedent('''
    blob
    mark :1
    data 21
    1
    2
    3
    4
    5
    6
    7
    8
    9
    10

    blob
    mark :2
    data 24
    1
    2
    3
    4
    five
    6
    7
    8
    9
    10

    blob
    mark :3
    data 22
    1
    2
    3
    4
    5
    6
    7
    8
    9
    ten

    blob
    mark :4
    data 6
    hello

    reset refs/heads/O
    commit refs/heads/O
    mark :5
    author Little O. Me <me@little.net> 1234567890 -0700
    committer Little O. Me <me@little.net> 1234567890 -0700
    data 2
    O
    '''[1:]))

  for x in range(1, i+1):
    p.stdin.write('M 100644 :1 somefile-%06d.txt\n' % x)
    p.stdin.write('M 100644 :4 world-%06d.txt\n' % x)
  p.stdin.write(textwrap.dedent('''

    reset refs/heads/A
    commit refs/heads/A
    mark :6
    author Little O. Me <me@little.net> 1234567890 -0700
    committer Little O. Me <me@little.net> 1234567890 -0700
    data 2
    A
    from :5
    '''[1:]))

  for x in range(1, i+1):
    p.stdin.write('M 100644 :2 somefile-%06d.txt\n' % x)
  p.stdin.write(textwrap.dedent('''

    reset refs/heads/B
    commit refs/heads/B
    mark :7
    author Little O. Me <me@little.net> 1234567890 -0700
    committer Little O. Me <me@little.net> 1234567890 -0700
    data 2
    B
    from :5
    '''[1:]))

  for x in range(1, i+1):
    p.stdin.write('M 100644 :3 somefile-%06d.txt\n' % x)
  p.stdin.close()
  p.wait()
  t['fast-import'] = time.time()-start

  start = time.time()
  subprocess.call('git checkout --quiet A'.split(), cwd="stupid")
  t['checkout'] = time.time()-start

  start=time.time()
  subprocess.call('GIT_MERGE_VERBOSITY=0 git merge --no-edit --quiet B',
                  cwd="stupid", shell=True)
  t['merge'] = time.time()-start

  print("%7d: %6.1f %7.1f %5.1f %6.1f" %
        (i, t['rmtree'], t['fast-import'], t['checkout'], t['merge']))


### Results (on mac):
#   size: rmtree fast-im check  merge
#     10:    0.0     0.0   0.0    0.0
#    100:    0.0     0.0   0.1    0.1
#   1000:    0.0     0.1   0.6    0.6
#   8192:    0.1     1.8   4.0    4.2
#  16384:    1.3     6.5   8.0    9.2
#  32768:    2.5    24.4  16.2   20.9
#  65536:    4.6    98.3  33.1   46.6
# 131072:    9.1   396.1  67.9  115.2
# 262144:   20.0  1684.9 130.2  343.1
# 524288:   50.5  6480.9 259.5 1236.7
#1048576:  103.7 29299.5 516.4 4813.4
