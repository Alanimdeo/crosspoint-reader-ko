# Fork-specific guidance

This file holds rules that apply only to this fork. It supplements the
repository guide: the root `AGENTS.md` and `.agents/rules/` still apply and take
precedence on any conflict.

## Repository context

- `origin` is this personal fork; `upstream` is the shared Korean release
  repository. Integration work targets `release/korean` on both.
- Fork-only features are merged with `upstream/release/korean`; keep them
  separable so an upstream merge stays reviewable.

## Rules

### Development build version

Dev builds report the upstream version plus a fork build number.

- Take the base from `[crosspoint] version` in `platformio.ini`, for example
  `2.0.0-ko.1`; do not edit that value for a dev build.
- Pass the full string through `CROSSPOINT_VERSION`, which
  `scripts/git_branch.py` uses verbatim instead of `-dev-<branch>-<sha>`:

  ```sh
  CROSSPOINT_VERSION=2.0.0-ko.1-dev.2 pio run -e default
  ```

- `<n>` counts dev builds since the base version last changed: the first dev
  build after an upstream merge or version bump is `-dev.1`, then it increments
  once per build. Two completed tasks and two builds end at `-dev.2`.
- If the current count is not known from the session, ask the human instead of
  guessing.
- Only `default` and `sticky` honor the override; other dev profiles set
  `CROSSPOINT_VERSION` in `platformio.ini` and ignore the variable.
- Release environments are unaffected. Never put a `-dev.<n>` value into the
  shared `platformio.ini`.
