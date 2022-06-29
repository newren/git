#!/usr/bin/env python

import re
import subprocess

cmd = "git describe --match v2[0-9.]* --exclude *-rc* origin/seen | sed -e s/-.*//"
last_tag = subprocess.check_output(cmd, shell=True, text=True).strip()
#last_tag = 'v2.34.1'
wantEveryonesTopics = False

refs = [last_tag,] + "v2.35.0 v2.36.1 origin/master origin/next origin/seen".split()
count = {}
out = dict()
for pair in zip(refs, refs[1:]):
    ref1, ref2 = pair
    if wantEveryonesTopics:
      cmd = f"git log --format=%h:%s --grep=^Merge.branch..../ {ref1}..{ref2}"
    else:
      cmd = f"git log --format=%h:%s --grep=^Merge.branch..en/ {ref1}..{ref2}"
    output = subprocess.check_output(cmd.split(), text=True)
    fields = [l.split(':') for l in output.splitlines()]
    hashes, lines = zip(*fields) if fields else ([], [])
    topics = [re.sub(".*'([^']*)'.*", r"\1", line) for line in lines]
    for sha, topic in zip(hashes, topics):
        cmd = f"git rev-list --count {sha}^1..{sha}^2"
        count[topic] = subprocess.check_output(cmd.split(), text=True).strip()
    out[pair] = set(topics)

seen = {}
i = 0
for pair in zip(refs, refs[1:]):
    new = out[pair].difference(seen)
    print(f"{pair[1]}:")
    for line in sorted(new):
        seen[line] = i
        print(f"    {count[line]:>2} {line}")
    i += 1
    print()

pulls=subprocess.check_output("curl -s 'https://api.github.com/search/issues?q=is:open+is:pr+author:newren+repo:git/git+repo:gitgitgadget/git+-label:seen' | jq -r .items[].pull_request.url", shell=True, text=True).splitlines()
print("Not picked up:")
for pull in pulls:
    output = subprocess.check_output(f"curl -s {pull} | jq -r '.head.ref, .commits'",
                                     shell=True, text=True)
    branch, commits = output.splitlines()
    print(f"    {commits:>2} {branch}")

#for line in sorted(seen):
#    print(" "*seen[line]*20 + line)
    
#    "git log --format=%s --grep=^Merge.branch..en/ {}..{}" | sed -e s/.into.*// | sort | uniq) <(git log --format=%s --grep=^Merge.branch..en/ origin/master..origin/next | sed -e s/.into.*// | sort | uniq)
# v2.34.1
