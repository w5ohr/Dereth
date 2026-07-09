# Admin credential rotation & history hygiene (#440 / #459)

The **code** vulnerability from #440 (hardcoded Admin password, force-reset every startup) is fixed:
`seed_admin` in `server/dereth_server.py` sources the password from `DERETH_ADMIN_PW`, seeds the
account only when it is **absent**, and never clobbers an existing password. See PR #444.

A source edit cannot undo two things, which this runbook covers:

1. The leaked literal `Tatia0623!` still lives in **git history** (`git log -S "Tatia0623" -- server/dereth_server.py` → introduced in `fec893b0`, removed in `1cec817e`). Anyone with clone/fork access can recover it.
2. Any **already-deployed** database was seeded with the leaked password and, because the fix
   deliberately never overwrites, still authenticates it until rotated.

> **Treat `Tatia0623!` as compromised.** Rotate it everywhere it was used or reused.

---

## 1. Rotate the credential on every deployment

Pick a fresh, strong secret and set it in the environment (systemd unit, container env, etc.):

```bash
export DERETH_ADMIN_PW='<a fresh strong secret>'
```

- **Fresh DB (no Admin row yet):** nothing more to do — `seed_admin` creates the account with the
  new secret on first start.
- **Existing DB (already seeded with the leaked password):** the fix won't overwrite the stored
  hash, so do a **one-shot rotation**. Set the rotate flag for a *single* restart:

  ```bash
  export DERETH_ADMIN_PW='<a fresh strong secret>'
  export DERETH_ADMIN_PW_ROTATE=1     # one-shot: re-writes the existing Admin password
  # ...restart the server once...  (log prints: "Admin password rotated to the current DERETH_ADMIN_PW")
  unset DERETH_ADMIN_PW_ROTATE        # IMPORTANT: unset so normal restarts stay non-destructive
  # ...restart again without the flag...
  ```

  (Equivalent manual alternative: delete the `Admin` row so the next start re-seeds it —
  `sqlite3 dereth.db "DELETE FROM users WHERE username='Admin';"` — but the flag is safer because it
  keeps the account and only swaps the password.)

## 2. Verify the old password is dead

On **every** environment, confirm a login with `Tatia0623!` now fails and the new secret works.
A quick check with the test client, or:

```bash
sqlite3 dereth.db "SELECT username, substr(pw,1,12) FROM users WHERE username='Admin';"
# the pw hash must differ from the pre-rotation value
```

## 3. Scrub the secret from git history — ⚠ destructive, coordinate first

This rewrites **every commit SHA**, breaks all open branches/PRs and existing clones, and requires a
force-push. **Do it only when the tree is quiet** — no open PRs mid-review, no other collaborator
mid-work (e.g. the M3 economy work), and pause the scheduled cloud routine first. Announce it so
everyone re-clones afterward.

```bash
# with git-filter-repo (preferred):
pip install git-filter-repo
echo 'Tatia0623!==>REDACTED-ROTATED-CREDENTIAL' > /tmp/redact.txt
git filter-repo --replace-text /tmp/redact.txt
git push --force --all
git push --force --tags

# then EVERY collaborator must re-clone (old clones still contain the secret):
#   rm -rf dereth && git clone <url> dereth
```

Confirm afterward: `git log -S "Tatia0623"` returns nothing.

> Because this rewrites history, it is intentionally **not** automated and not done by the
> issue-fixing routine. A repo maintainer runs it deliberately.

## 4. Confirm no other copies linger

- **Local build artifacts:** `server/__pycache__/*.pyc` compiled from the old code embed the literal
  as a string constant. They're gitignored (never committed), but delete/recompile them on any
  machine that ran the old server:
  ```bash
  rm -rf server/__pycache__
  ```
- Check **CI logs**, **deployment snapshots/backups**, and any **secret managers** for the string,
  and purge/rotate wherever it appears.

---

## Done when

- A new `DERETH_ADMIN_PW` is in force on all deployments and `Tatia0623!` fails to authenticate everywhere.
- (If pursued) `git log -S "Tatia0623"` is empty after a history rewrite + force-push, and all clones are refreshed.

*The server code change for this issue is only the `DERETH_ADMIN_PW_ROTATE` one-shot helper; steps 1–4
are operator actions that the code cannot perform for itself.*
