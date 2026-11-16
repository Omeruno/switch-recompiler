# CAPTCHA compatibility audit — 0.17.0

## Result

**Interactive reCAPTCHA / Yandex SmartCaptcha completion is not supported yet.**
0.16.0 added actual child iframe documents/runtimes, anonymous HTTPS loading,
clipped painting and native clicks. 0.17.0 identifies and removes the observed
strict TextDecoder failure inside the provider runtime. A new public demo capture
advances to `ReferenceError: Worker is not defined`. The original decoder SyntaxError
is no longer the observed blocker. No usable checkbox or challenge completion is
claimed; runtime loading does not establish provider acceptance.

The reason Google/Yandex request verification on the user's network is not
established. Host observations do not reproduce the console's IP/session/service
risk decision. No user credentials, challenge answers, cookies or storage were
needed for this investigation. UA impersonation, challenge bypass and automatic
CAPTCHA solving are not implemented.

## Observations on 2026-11-13

Public resources were inspected using the existing SwitchVisualBrowser identity:

| Resource | Observed bytes | Existing failure |
| --- | ---: | --- |
| Google reCAPTCHA API loader | 978 | Injects crossOrigin=anonymous and SHA384 integrity |
| Google reCAPTCHA runtime from gstatic | 861664 | Exceeds former 128 KiB script quota; uses frames, channels, random values and base64 |
| Yandex SmartCaptcha captcha.js | 160785 | Exceeds former 128 KiB script quota; creates iframe and uses channels/messages |

The asset versions and sizes can change. Public Google/Yandex search responses
were HTTP 200 in the cloud; this is not evidence that verification is unnecessary
on Switch or that search results are fully usable.

The native host opened `https://www.google.com/recaptcha/api2/demo` without
submitting the form or solving a challenge. After the loader fixes, its first
observed TypeError was an Element.getElementsByTagName call. Live Element/Document
HTMLCollection bindings remove that failure. At the 0.14.0 checkpoint, the subsequent observed blocker was
`ReferenceError: MessageChannel is not defined`; the native iframe limitation
message is visible. A checkbox/challenge is still not rendered.

## Earlier loader/API changes — 0.14.0

- Classic scripts: 2 MiB per source and 8 MiB cumulative source budget per realm.
  Resource, script-count, JS heap, CSP and execution budgets remain bounded.
  Compilation and guest execution have separate phases; bounded parsing does not
  consume the guest's existing execution budget.
- Anonymous cross-origin classic scripts use the existing actual TLS/CORS
  transport. Redirect policy is checked before following. Cross-site credentialed
  requests remain unsupported; no cookie policy is weakened.
- Script SRI checks SHA256/SHA384/SHA512 using Mbed TLS, selecting the strongest
  recognized algorithm. Wrong hashes are rejected. Cross-origin integrity requires
  an eligible CORS response, including after redirects. nosniff rejects non-JS MIME.
  Unknown hash algorithms do not supply integrity; malformed recognized digests
  fail closed. Fetch/module integrity options are still unsupported.
- Latin-1 btoa / forgiving-base64 atob, bounded to 1 MiB input.
- Crypto.getRandomValues fills real integer TypedArray backing from the OS random
  source, preserves view bounds and rejects floating views / >65536 bytes. Subtle
  crypto and randomUUID remain absent. isSecureContext derives from native URL.
- Native click/keyboard/pointer dispatch marks Event.isTrusted. Script .click()
  and dispatchEvent remain synthetic; redispatch clears trust. Fake/proxy Event
  objects are rejected. Native clicks include MouseEvent type and client position.
  This is event semantics, not proof of human identity or service acceptance.
- Live HTMLCollection for Element/Document tag/class queries, indexed access,
  item/namedItem and iteration. Attribute/child mutations invalidate cached lists.
- Visible, noninteractive text for unsupported visible iframe elements; hidden
  trackers do not produce a visible panel. Reports list frame count and the absent
  cross-window capabilities. No child content executes in the parent's realm.

## Architecture recorded before 0.16.0

Real iframe support needs separate documents and JS realms, per-frame URL/origin,
clipped painting and native focus/input, lifecycle and navigation cancellation.
Parent frame-src and child frame-ancestors/X-Frame-Options/sandbox must be enforced
before rendering. Cross-origin WindowProxy must restrict DOM/storage access.
The implemented postMessage/MessagePort broker supplies source/origin checks,
asynchronous delivery, ownership, cloning, quotas and teardown; iframe lifecycle
integration must preserve these guarantees. Same-origin
frame DOM access and third-party credentials also require explicit policy.

Those mechanisms need owned multi-origin tests before attempting provider
compatibility. Sharing the parent's window/document with a foreign iframe would
break the origin boundary and is not an acceptable implementation.


## 0.15.0 follow-up

The owned MessageChannel/structuredClone fixture passes both the native host and
Chromium 151.0.7922.34. A separate host harness creates two QuickJS runtimes with
native parent/child URL origins and checks source identity, cross-origin DOM denial,
cyclic Map/typed-buffer cloning, transferred port replies, teardown and quota
atomicity. Neither runtime is installed into an HTML iframe yet.

A fresh native capture of the public Google reCAPTCHA demo on 2026-11-13 loaded
its page scripts without the former MessageChannel ReferenceError. It still shows
the unsupported-iframe explanation rather than a checkbox. The form was not
submitted and no challenge was solved. No claim is made about SmartCaptcha or
Google/Yandex search verification on the user's network.

The next required integration remains actual child documents: separate resource
and script lifecycles, per-frame origins, clipped painting and input; parent CSP
frame-src/child-src and child frame-ancestors/X-Frame-Options checks on every
navigation; sandbox policy and an explicit credential/storage policy. The native
broker is ready for this integration, but it does not replace these boundaries.


## 0.16.0 iframe integration

The owned two-origin TLS fixture verifies actual child HTML/CSS/script fetching,
separate documents and QuickJS runtimes, clipped RGBA pixels, mouse coordinates and
trusted native click events, checkbox state, resize without state reset, messaging,
stable source identity during navigation and new identity after reinsertion.
Transport counters verify no cookie leakage and no contact with a denied redirect
target. Response frame-ancestors/XFO, CSP fallback/intersection and sandbox/srcdoc
rejection are checked before child script execution. The packaged Russian offline
iframe exercises the same renderer without an Internet connection.

A host capture of https://www.google.com/recaptcha/api2/demo with a ten-second wait
on 2026-11-13 reports iframe loaded=1 and SyntaxError from the anchor runtime.
The frame is painted, but no usable checkbox is shown. The observed public runtime
converts a caught RangeError into SyntaxError; its original cause is unconfirmed.
No form submission, challenge answer or automated solving was performed. SmartCaptcha
and the user's actual Google/Yandex verification sessions are not verified.

Next investigation: isolate the provider JS execution/DOM compatibility failure
using owned reproducers, without changing browser identity or raising stack/CPU
limits blindly. Complete input/editing, same-origin DOM/initial blank contexts,
opaque sandbox origins and an explicit partitioned credential/storage design remain
separate requirements. The browser currently blocks unsupported embedding modes
and uses anonymous frame requests; these restrictions can independently prevent
some verification/login flows.


## 0.17.0 strict decoder diagnosis

A temporary host-only QuickJS throw trace, enabled only for the public demo,
observed RangeError `Only nonfatal UTF-8 decoding is supported` at our TextDecoder
constructor before the provider converted it to SyntaxError. The current public
asset release was `4i3aN5wAweL7i3ADI7BSX4B-`. Restoring the exact upstream QuickJS
file and implementing strict/streaming encoding removed that observed failure.
Stack size, interrupt budget and browser identity policy were not relaxed.

The native host captured https://www.google.com/recaptcha/api2/demo again without
submitting the form or clicking a challenge. The loaded anchor reports
`ReferenceError: Worker is not defined`. Dedicated workers remain absent. This
is an actual next API requirement; installing a no-op Worker or executing its code
in the parent document would misrepresent support and break isolation.

Owned encoding tests compare positive/negative cases and byte-index fingerprints
with Chromium. They do not solve or submit provider challenges. SmartCaptcha and
Google/Yandex verification on the user's network remain unverified. The required
next architecture is a real dedicated-worker runtime with message transfer,
script/Blob loading, CSP, origin/credentials policy, cancellation and bounded
resources, followed by further provider compatibility checks.
