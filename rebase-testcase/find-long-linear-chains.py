#!/usr/bin/env python3

#
# Examples:
#   ./find-long-linear-chains.py ../linux-stable
#   ./find-long-linear-chains.py .
#

import collections
import os
import re
import subprocess
import sys

if len(sys.argv) > 1 and os.path.isdir(sys.argv[1]):
  os.chdir(sys.argv[1])
match, total = 0, 0
p = subprocess.Popen(['git', 'log', '--reverse', '--format=%H %ad %P',
                      '--date=short', '--topo-order', 'origin/master'],
                     text=True, stdout=subprocess.PIPE)
dates = {}
klasses = {}
connections = {}
sizes = collections.defaultdict(set)
tails = collections.defaultdict(set)
for line in p.stdout:
  line = line.rstrip()
  if not line:
    continue
  fields = line.split()
  commit, date, parents = fields[0], fields[1], fields[2:]
  assert(all(p in klasses for p in parents))
  if len(parents) > 1:
    klass, count = commit, 0
  elif len(parents) == 0:
    klass, count = commit, -5000
  else:
    klass, count = klasses[parents[0]]
    if klass in sizes[count]:
      #del sizes[count][klass]
      sizes[count].remove(klass)
      del connections[(count, klass)]
    count += 1
  klasses[commit] = (klass, count)
  dates[commit] = date
  sizes[count].add(klass)
  connections[(count, klass)] = commit

for count, klass in sorted(connections, reverse=True):
  commit = connections[(count, klass)]
  print("  {}: {} {} ({})".format(count, commit, dates[commit], klass))
