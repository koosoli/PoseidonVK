# Implementation Direction

For agreed renderer and gameplay goals, implement toward the complete target architecture rather than repeatedly tuning temporary placeholders.

- Identify the reference implementation and the required data flow, resources, synchronization, and validation before editing.
- Do not present a visual approximation as completion when it cannot satisfy the agreed behavior.
- Use temporary fallbacks only to keep the build runnable or when the user explicitly requests an experiment; label them clearly and schedule their removal.
- Batch related subsystem work into complete milestones. Do not ask for a manual smoke test after minor intermediate edits.
- Before a milestone smoke test, provide an exact launch command and a concise checklist covering intended behavior and likely regressions.
- When a reference renderer uses a larger architecture, port the necessary architecture instead of attempting to imitate its result with isolated shader tweaks.
