# Deferred changes

Changes that were evaluated and deliberately not made. Each entry records
what the change would do, why it was deferred, and what would justify
revisiting it.

## Exact "done" signal for agent tabs

Status: not implemented. Evaluated 2026-09-28.

### Current behavior

The tab activity indicator (`tab-activity.c`, fed by `tab_on_output()` in
`tab.c`) infers an agent's state from its output. A hidden tab whose
foreground process is listed in `[tab-bar] activity-pulse-processes`
(claude, codex, agy) counts as working after a run of output lasting
`TAB_ACTIVITY_MIN_RUN_MS` (1 s), and as done after
`activity-pulse-quiet-ms` (700 ms) of silence.

### Proposed change

Use the notifications the agents themselves can send as an exact signal,
and keep the output heuristic as the fallback:

- When a hidden tab running a matched agent receives OSC 9, OSC 777 or BEL,
  mark it done immediately.
- Otherwise the current heuristic applies unchanged, so an agent that is
  not configured behaves as it does today.
- Document in `README.md` the one setting per agent that turns its signal on.

foot already parses these sequences (`osc.c`: OSC 9, OSC 777, and OSC 133
C/D) and turns OSC 9 and OSC 777 into desktop notifications. It ignores
OSC 9;4 (ConEmu progress).

### What each agent can send

| Agent | Signal | Setting | Default in foot |
|---|---|---|---|
| Claude Code | OSC 9, OSC 777 or BEL, or anything from a hook | `preferredNotifChannel` (`iterm2`, `ghostty`, `terminal_bell`) | Nothing: `auto` notifies only in iTerm2, Ghostty and Kitty |
| Codex | OSC 9, falling back to BEL | `[tui] notifications`, `notification_method`, `notification_condition` | Off, and only fires when Codex thinks the terminal is unfocused |
| agy | BEL, plus its own desktop notification | `notifications` | Off |

### Why it was deferred

- The heuristic already gets all three agents right in practice. No false
  or missed "done" has been observed since the 1 s minimum run was added.
- Every agent needs a setting changed before it sends anything, so the
  heuristic has to stay regardless, and a fresh setup gains nothing.
- The signals do not say which event fired. Telling "finished" apart from
  "needs your approval" would mean parsing message text.
- Codex notifies only when it believes it is unfocused, and a tab switch
  does not send focus events: `do_tab_switch()` in `tab.c` moves the focus
  flags without writing `CSI I` / `CSI O` to either pty. A hidden Codex tab
  therefore stays silent unless `notification_condition = "always"` is set,
  or the fork starts sending focus events on tab switch (which changes how
  all three agents behave and needs its own testing).
- Enabling an agent's notifications alongside a hook that already sends
  OSC 777 can produce two desktop popups per event.
- There is no shared "working" signal. Claude Code sends OSC 9;4 progress
  only to ConEmu, Ghostty and iTerm2, so working still has to come from
  output.

### When to revisit

- The indicator flashes done while an agent is still working, or misses a
  finish.
- An agent starts sending a signal by default in foot, or a signal that
  distinguishes a finished turn from a pending approval.

### Related idea

For plain shell tabs, OSC 133 D (sent by shell integration when a command
finishes, with its exit code) could mark a hidden tab when a long command
ends, colored by success or failure. This needs a shell integration snippet
in zsh and is independent of the agent signal.
