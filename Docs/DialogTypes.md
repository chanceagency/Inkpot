# Dialog Types v1 (pointer)

Not implemented in this plugin yet. This document records how Inkpot maps
onto the cross-engine dialog specification so the P3 port starts from the
right place.

The specification, the TypeScript reference scheduler and the golden traces
live in the Prosetta web repository:

- `docs/features/2026_11_05_dialog_types/` — behavior spec, decisions, open questions, work breakdown, canonical `.ink` fixtures.
- `lib/dialog-scheduler/` — `types.ts`, `vocabulary.ts`, `resolve.ts`, `scheduler.ts`, `inkjs-port.ts`.
- `tests/dialog-scheduler/traces/*.json` — host-event timelines with the expected decision log and host calls. The C++ port replays these through `UInkpotStory`.

## What changes for Inkpot (P3)

- `StoryPort` maps directly onto `UInkpotStory`: `SwitchFlow`,
  `SwitchToDefaultFlow`, `RemoveFlow`, `GetCurrentFlowName`,
  `ChoosePathString`, `ChooseChoiceIndex`, `CanContinue`, `Continue`,
  `GetCurrentTags`, `GetCurrentChoices`, `TagsForContentAtPath`.
  `currentScope()` is the walk `UInkpotLine::GetInheritedTags` already does.
- The scheduler owns line-in-flight. The Unreal host calls
  `NotifyLineRenderBegin` inside `playLine` and `NotifyLineRenderEnd` inside
  `lineFinished` so `Continue` is never refused for the active flow, and it
  **ends the render before `RemoveFlow`**: `RemoveFlow` does not clear
  `LineRenderContextsInFlight` or `FlowsPendingEnd` for the removed flow.
- `OnFlowEnded` is not relied on; the scheduler detects the end of a flow
  itself.
- `FProsettaVoicePolicyTags::Resolve` reads `dialog_voice` as the canonical
  key; the bare `interrupt` / `overlap` / `queue` tags remain aliases.
- `UProsettaSpeakerComponent` identity supplies the actor key for
  exclusivity; `CreateVoice` stays the transport behind `playLine`.
- `EXTERNAL`s bound with `bLookAheadSafe = true` can fire twice; events they
  raise must go through `scheduler.request`, never `SwitchFlow` directly.
