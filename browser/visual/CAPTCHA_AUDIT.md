# CAPTCHA compatibility audit — 0.14.0

## Result

**Interactive reCAPTCHA / Yandex SmartCaptcha completion is not supported yet.**
This release fixes several confirmed loader and DOM failures. The browser still
has no child browsing contexts, cross-window postMessage or MessageChannel/port
transfer. A nonworking widget cannot be repaired by declaring those features
supported. Sites can also independently reject an experimental browser.

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
HTMLCollection bindings remove that failure. The subsequent observed blocker is
`ReferenceError: MessageChannel is not defined`; the native iframe limitation
message is visible. A checkbox/challenge is still not rendered.

## Implemented changes

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

## Required next architecture

Real iframe support needs separate documents and JS realms, per-frame URL/origin,
clipped painting and native focus/input, lifecycle and navigation cancellation.
Parent frame-src and child frame-ancestors/X-Frame-Options/sandbox must be enforced
before rendering. Cross-origin WindowProxy must restrict DOM/storage access.
postMessage needs source/origin validation and asynchronous delivery; MessagePort
transfer needs real endpoint ownership, cloning, quotas and teardown. Same-origin
frame DOM access and third-party credentials also require explicit policy.

Those mechanisms need owned multi-origin tests before attempting provider
compatibility. Sharing the parent's window/document with a foreign iframe would
break the origin boundary and is not an acceptable implementation.
