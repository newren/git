#!/usr/bin/env python3

#
# Examples:
#   ./count-renames.py ../linux-stable: Matched 21729/28416 basenames (76.47%)
#   ./count-renames.py ../gcc:          Matched 6171/9598 basenames (64.29%)
#   ./count-renames.py ../gecko-dev:    Matched 151921/191958 basenames (79.14%)
#

import os
import re
import subprocess
import sys

class PathQuoting:
  _unescape = {'a': '\a',
               'b': '\b',
               'f': '\f',
               'n': '\n',
               'r': '\r',
               't': '\t',
               'v': '\v',
               '"': '"',
               '\\':'\\'}
  _unescape_re = re.compile(r'\\([a-z"\\]|[0-9]{3})')
  _escape = [chr(x) for x in range(127)]+[
             '\\'+oct(x)[2:] for x in range(127,256)]
  _reverse = dict(map(reversed, _unescape.items()))
  for x in _reverse:
    _escape[ord(x)] = '\\'+_reverse[x]
  _special_chars = [len(x) > 1 for x in _escape]

  @staticmethod
  def unescape_sequence(orig):
    seq = orig.group(1)
    return PathQuoting._unescape[seq] if len(seq) == 1 else bytes([int(seq, 8)])

  @staticmethod
  def dequote(quoted_string):
    if quoted_string.startswith('"'):
      assert quoted_string.endswith('"')
      return PathQuoting._unescape_re.sub(PathQuoting.unescape_sequence,
                                          quoted_string[1:-1])
    return quoted_string

  @staticmethod
  def enquote(unquoted_string):
    # Option 1: Quoting when fast-export would:
    #    pqsc = PathQuoting._special_chars
    #    if any(pqsc[x] for x in set(unquoted_string)):
    # Option 2, perf hack: do minimal amount of quoting required by fast-import
    if unquoted_string.startswith('"') or '\n' in unquoted_string:
      pqe = PathQuoting._escape
      return '"' + ''.join(pqe[x] for x in unquoted_string) + '"'
    return unquoted_string

if len(sys.argv) > 1 and os.path.isdir(sys.argv[1]):
  os.chdir(sys.argv[1])
quoted_string_re = re.compile(r'"(?:[^"\\]|\\.)*"')
match, total = 0, 0
cmd = 'git -c diff.renameLimit=30000 log --format=%n --name-status --diff-filter=R origin/master'
p = subprocess.Popen(cmd.split(), text=True, stdout=subprocess.PIPE)
for line in p.stdout:
  line = line.rstrip()
  if not line:
    continue
  assert line.startswith('R')
  if True:
    line = line[line.find('\t')+1:]
    if line.startswith('"'):
      m = quoted_string_re.match(line)
      if not m:
        raise SystemExit("Couldn't parse rename source in {}".format(line))
      orig = PathQuoting.dequote(m.group(0))
      new = line[m.end()+1:]
    else:
      orig, new = line.split('\t', 1)
      if new.startswith('"'):
        new = PathQuoting.dequote(new)
  else:
    try:
      (rename, orig, new) = line.split('\t')
    except ValueError:
      continue
  
  if os.path.basename(orig) == os.path.basename(new):
    match += 1
  #print("No match for {}: ({}) vs ({})".format(line, orig, new))
  total += 1

print("Matched {}/{} basenames ({:4.2f}%)"
      .format(match, total, 100.0*match/total))
