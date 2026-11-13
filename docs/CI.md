# CI: our own, on a build computer

The repository has no GitHub Actions workflows and uses no GitHub-hosted
minutes. Its CI is `tools/ci`, a small program of ours. On a timer it:

1. fetches the repository into a clone of its own;
2. finds every branch head it has not checked yet;
3. runs `tools/check` on it, in a clean worktree at that commit, with a
   scratch `HOME`;
4. posts the result to GitHub as a **commit status** (context
   `tools/check`) through the REST API.

A pull request then shows green or red, like any CI. `tools/check` covers:

- GCC and Clang builds, with and without ASan/UBSan;
- every test, the golden data, and the dependency and clean-room guards;
- the headless runs, the GPU and software pictures, the sound and the frame
  times;
- the MinGW cross-build of `TailonLock.exe`.

Everything here is our own code, running on the operating system and git.
The only outside party is GitHub's REST API, which `tools/ci` uses to post
statuses.

## How a round goes

- `git fetch --prune origin` in the CI clone.
- For each remote branch that matches `BRANCHES`, if its head is not in the
  state file:
  - post `pending`, "tools/check is running";
  - add a worktree at that commit in a temporary folder and run
    `tools/check` there;
  - post `success` or `failure` with the summary (for example "failed:
    picture, perf (81 s)"), or `error` if the check could not run at all;
  - keep the report in the state folder, the last `KEEP` per branch.
- Remove the worktree and record the commit as checked, so a restart does not
  check it again.

Only branch heads of the repository itself are checked. Pull requests from
forks never reach the machine, because the CI only fetches `origin`.

## Security

- **Its own user.** It runs as an unprivileged user (`tlci` below) with no
  sudo. It cannot read the main user's home, which is `drwx------`.
- **A narrow token.** The token is a fine-grained personal access token for
  **this repository only**, with exactly two permissions: **Commit statuses:
  read and write**, and **Contents: read-only** (so the clone can fetch).
  - It sits in a file only `tlci` can read (`chmod 600`). `tools/ci` refuses a
    token file anyone else can read.
  - Give it an expiry date and renew it.
- **What runs.** The CI runs the code of the repository's own branches,
  exactly what a developer runs with `tools/check`.
- **The GPU.** `/dev/dri/renderD128` is open to every user, so `tlci` reaches
  the GPU for the picture and performance stages without joining a group.
  The compilers, sanitizer runtimes, MinGW and glslang are system packages.
- **The Windows VM** stays out of CI. It is driven by hand, through
  `tools/vm_run`, which takes the VM lock file so only one thing uses the VM
  at a time.

## One-time setup

Run as the main user. Make the token on GitHub first: **Settings > Developer
settings > Personal access tokens > Fine-grained tokens > Generate new
token**. Choose resource owner: you; repository access: only this repository;
permissions: Commit statuses read and write, Contents read-only. Copy the
token.

```sh
# 1. the user: no sudo, no login shell
sudo useradd --system --create-home --home-dir /var/lib/tlci --shell /usr/sbin/nologin tlci

# 2. its token, readable by it alone (paste the token, then Ctrl-D)
sudo -u tlci -s /bin/bash -c 'umask 077 && mkdir -p ~/.config/tailonlock-ci && cat > ~/.config/tailonlock-ci/token'

# 3. its clone, fetched with the same token (it is never pushed from)
sudo -u tlci -s /bin/bash -c 'cd ~ && git clone "https://x-access-token:$(cat ~/.config/tailonlock-ci/token)@github.com/OWNER/REPO.git" clone'

# 4. its settings
sudo -u tlci -s /bin/bash -c 'cat > ~/.config/tailonlock-ci/ci.conf <<EOF
CLONE=/var/lib/tlci/clone
REPO=OWNER/REPO
TOKEN_FILE=/var/lib/tlci/.config/tailonlock-ci/token
PRIVATE=/var/lib/tlci/.config/tailonlock-ci/check-private
INTERVAL=120
BRANCHES=*
EOF'

# 4b. the checker's private term list (personal and machine names, never in the repo): copy it in
sudo install -o tlci -m 600 .check-private /var/lib/tlci/.config/tailonlock-ci/check-private

# 5. one dry round, which posts nothing, to see it work
sudo -u tlci -s /bin/bash -c 'cd ~/clone && tools/ci --once --dry-run'

# 6. the service
sudo tee /etc/systemd/system/tailonlock-ci.service >/dev/null <<EOF
[Unit]
Description=Tailon Lock CI (tools/ci)
After=network-online.target
Wants=network-online.target

[Service]
User=tlci
WorkingDirectory=/var/lib/tlci/clone
ExecStart=/var/lib/tlci/clone/tools/ci
Restart=on-failure
RestartSec=60
Nice=10
NoNewPrivileges=yes
ProtectSystem=strict
ReadWritePaths=/var/lib/tlci /tmp
PrivateTmp=yes

[Install]
WantedBy=multi-user.target
EOF
sudo systemctl daemon-reload && sudo systemctl enable --now tailonlock-ci
journalctl -u tailonlock-ci -f      # watch it
```

`tools/ci` runs from the clone, so the CI always runs the newest version of
itself that has been fetched. To stop it: `sudo systemctl disable --now
tailonlock-ci`. To remove it as well: `sudo userdel -r tlci`, then delete the
token on GitHub.

## Notes

- Branch protection can require the `tools/check` status before a merge:
  Settings > Branches > add a rule > "Require status checks to pass" and
  choose `tools/check`.
- `tools/check` keeps its large references (the full JSON dumps and the
  200-second music render) in `build/check/ref/`, outside git. A fresh
  worktree does not have them, so stage 8 compares the render by its
  checked-in hash. That is still exact, but on a mismatch it cannot report
  how large the difference is.
- The frame-time baselines in `tests/golden/perf.txt` belong to the machine
  that took them.
- A run takes about 75-110 s, using all cores and the GPU.
