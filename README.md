$ git clone git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
$ cd linux-stable
$ git checkout -b v5.4-renames v5.4.0
$ git mv drivers pilots
$ git commit -m "Rename drivers -> pilots"
$ git branch -f hwmon-updates fd8bdb23b91876ac1e624337bb88dc1dcc21d67e
$ git rev-list --count 4703d9119972bf586d2cca76ec6438f819ffa30e..hwmon-updates


With an "old" version of git (e.g. git-2.25.0):

$ git config merge.directoryRenames true
$ time git -c merge.renameLimit=30000 rebase -m --onto HEAD 4703d9119972bf586d2cca76ec6438f819ffa30e hwmon-updates


Now build the new version of git from this branch and compare:
$ git checkout v5.4-renames
$ git branch -f hwmon-updates fd8bdb23b91876ac1e624337bb88dc1dcc21d67e
$ time git fast-rebase --onto HEAD 4703d9119972bf586d2cca76ec6438f819ffa30e hwmon-updates


You can see slides explaining the changes at:
  https://github.com/newren/presentations/blob/pdfs/merge-performance/merge-performance-slides.pdf
