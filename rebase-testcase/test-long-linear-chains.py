#!/usr/bin/env python3

import os
import re
import subprocess
import sys

def check_candidate(sha, oldsha, count):
  ''' returns (has_deletions, replacement_oldsha, new_count) '''

  cmd = 'git log --oneline --name-status {}..{}'.format(oldsha, sha)
  output = subprocess.check_output(cmd.split(), text=True)
  newbase = False

  for outline in output.splitlines():
    newbase = False
    if newbase:
      raise SystemExit("Yikers, newbase was True for {}".format(sha))
    m = re.match('^([0-9a-f]+)', outline)
    if m:
      commit = m.group(1)
    else:
      if re.match('^[RD]', outline):
        return (True, oldsha, count)
      elif re.match('^M\tMakefile', outline):
        newbase = True
  if newbase:
    oldsha = commit
    count -= 1
  return (False, oldsha, count)

def rebases_clean(sha, oldsha):
  subprocess.run('git checkout --quiet v5.4'.split(), check=True)
  subprocess.run('git clean -fdqx'.split(), check=True)
  subprocess.run('git branch -f soc-driver {}'.format(sha).split(), check=True)
  for path in ['.git/logs/refs/heads/soc-driver', '.git/logs/HEAD']:
    if os.path.exists(path):
      os.unlink(path)
  subprocess.run('rm -rf .git/objects/[0-9a-f]*', shell=True, check=True)
  subprocess.run('git status'.split(), capture_output=True, check=True)
  cmd = 'git rebase -m --onto HEAD {} soc-driver'.format(oldsha)
  p = subprocess.run(cmd.split(), capture_output=True)
  if p.returncode != 0:
    subprocess.run('git rebase --abort'.split())
    return False
  return True
  
def main():
  for line in sys.stdin:
    (count, sha, date, oldsha) = line.split()
    oldsha = oldsha[1:-1]
    count = int(count[0:-1])
    if not count:
      continue

    (has_deletions, oldsha, count) = check_candidate(sha, oldsha, count)
    if has_deletions:
      print("Deletions: {}, {}".format(count, sha))
      continue

    if rebases_clean(sha, oldsha):
      print("Wahoo!:    {}, {}".format(count, sha))
    else:
      print("Unclean:   {}, {}".format(count, sha))

main()
