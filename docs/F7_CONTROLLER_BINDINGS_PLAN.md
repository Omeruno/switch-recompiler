# F7 — user-configurable Switch controller → guest keyboard (planned)

User priority, 2026-10-30: configure the controller on Switch and bind buttons
without rebuilding the NRO. This is the next product/input package after D8.
**Not implemented or accepted yet.** D8's host key script is a diagnostic tool,
not this settings screen and not a deployed controller remapper.

## Product scope and defaults

Add a native keyboard-controls screen reachable through Start/settings, usable
with controller and touch. Select a physical button, choose a named PC key, apply,
save, restore defaults. Show current bindings and visible save/validation errors.
Use the actual D2 fixed-US key/scancode/name table. Letters/digits, arrows,
Space/Enter/Escape/Tab, F1–F12 and sided Shift/Ctrl/Alt must be selectable; an
explicit unbound action must be possible. A key binding is a real down/held/up
sequence, not an injected character, pasted text, WM_COMMAND or game-name hook.
Language/layout switching, IME and physical USB keyboards remain separate work.

First supported scope: one global guest keyboard profile, inherited by fresh
sessions and all EXEs using new USER. Existing L legacy/ZL software launch choices
and existing legacy input remain explicit; do not advertise unsupported legacy
remapping. Per-application profiles/chords/macros/analog-stick key modes may follow
with their own contracts. Keep pointer movement/touch and mouse-button bindings
separate. Preserve current defaults until the user applies a valid change.

Keep an always-available native escape/Start/quit route. Native shell navigation
must remain usable even when guest B/X/Y/arrows are rebound. The remapper must
know which physical press belongs to native UI versus the guest; opening/closing
Start cannot send a new key to the wrong owner. Define reserved native controls
visibly instead of silently accepting an ineffective binding.

## Common implementation and lifecycle requirements

- Put button→key translation in a platform-independent bounded core; main.cpp only
  supplies checked physical snapshots and receives events. Do not bury semantics
  in a second Switch-only table or special-case Pinball/Paint.
- Track emitted keys and physical owners. Two buttons bound to the same key must
  not produce a premature key-up when only one is released. Define repeat policy
  explicitly; retain genuine scan/side/extended/previous/up bits through USER.
- Apply profile/context changes atomically. Release old guest keys, then quarantine
  already-held physical controls until release. Do not synthesize a new press for
  a button held while leaving settings or entering a new game.
- Source disconnect, cancellation, normal exit/error, held-key abort and relaunch
  must not leave keys or modifiers stuck. Preserve paired queue transitions and
  explicit overflow handling in ShellSession's existing input service.
- Save through the real persistent profile/VFS with bounded, versioned validated
  data and actual flush/close/publication results. Existing sort/associations must
  survive migration; corrupt/truncated/future files must not erase a good profile.
  RAM-applied versus successfully saved state must be visible and distinct.

## Acceptance before claiming readiness

1. Native core tests for duplicate bindings, modifiers, repeats, unbound/reserved/
   invalid keys, context/profile changes while held, disconnect and overflow.
2. Real shell settings/controller/touch focus test: edit/apply/save/reset, close,
   reconstruct a fresh shell and read actual persisted values. Inject storage
   failures and retain the previous good on-disk profile and existing preferences.
3. Public x86 fixture through the **same production mapping core** → actual USER
   queue → GetKeyState/GetAsyncKeyState/scancode/WM_KEY*/TranslateMessage and visible
   output. Include held-key host abort and clean subsequent launch. Extend/reuse
   D8; do not replace it with a test that merely inspects a mapping array.
4. Strict no-exceptions/no-RTTI checks, full host/MinGW/Mesa and exact-published-SHA
   six-job CI/NRO/Pages/artifact audit. Switch physical buttons/touch remain a
   separate deferred gate until the user supplies evidence for that build.

The public game-like loop in [D8](D8_GAME_LOOP.md), existing [D2](D2_KEYBOARD_LAYOUT.md)
and [F4b input service](F4B_INPUT_SERVICE.md) are prerequisites/reference contracts.
No private game executable, profile or trace belongs in git/CI/Pages.
