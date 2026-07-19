\# Modern Vulkan Arma 2 Compatibility Project



\## Source-Aware Master Engineering Plan for AI Agents



\*\*Status:\*\* Authoritative project plan

\*\*Supersedes:\*\* All previous planning documents

\*\*Primary objective:\*\* Extend the existing Vulkan-modernized Arma 1-derived engine so it can progressively load and run Arma 2 content while preserving the modern renderer, GPU systems, performance improvements, and long-term maintainability.



\---



\# 1. Mission



Create a modern engine that can support multiple compatibility targets without allowing any one legacy game to define the engine’s internal architecture.



The intended conceptual structure is:



```text

Modern Engine Runtime

│

├── Shared compatibility infrastructure

├── Arma 1 compatibility

├── Arma 2 compatibility

└── Future native or additional compatibility modes

```



This is a conceptual dependency model, not a prescribed source-tree layout.



The agents working on the repository must inspect the actual codebase before deciding:



\* where module boundaries belong;

\* which systems are already generic;

\* which systems contain Arma 1-specific behavior;

\* which abstractions already exist;

\* whether compatibility modules should be libraries, plugins, registries, policies, adapters, namespaces, build targets, or another mechanism;

\* which parts should remain untouched.



The agents must adapt the plan to the real architecture rather than forcing the repository into a guessed design.



\---



\# 2. Target Outcome



The target engine should eventually be able to:



1\. Start in an Arma 2 compatibility mode.

2\. Mount a legally installed Arma 2 data set.

3\. Resolve required addons and configuration.

4\. Load supported Arma 2 models, materials, textures, animations, worlds, and mission data.

5\. Execute a progressively larger subset of Arma 2 scripting behavior.

6\. Reproduce supported Arma 2 gameplay behavior closely enough for missions and content to function.

7\. Render everything through the project’s modern Vulkan renderer.

8\. Preserve modern GPU-based systems and improvements.

9\. Continue supporting Arma 1 while that compatibility target remains enabled.

10\. Make either compatibility target removable without redesigning the engine.



The Vulkan implementation should remain authoritative for rendering.



Arma 2 compatibility should describe what content means, not how the original Arma 2 renderer implemented it.



\---



\# 3. Core Principle



\## Preserve external contracts, modernize internal implementation



Arma 2 content expects certain observable behavior.



The project should reproduce those observable contracts while remaining free to use a better internal implementation.



Examples:



\* An Arma 2 material definition may map to a modern Vulkan shader and PBR-like material representation.

\* An Arma 2 animation source may drive a modern animation graph and GPU skinning system.

\* Arma 2 world data may be converted into modern streaming cells and GPU terrain resources.

\* Arma 2 vegetation may use GPU instancing and compute culling.

\* Arma 2 ballistics may use jobs, SIMD, fixed-step simulation, or batching.

\* Arma 2 script commands may run through a modernized scripting runtime.

\* Arma 2 AI behavior may use a modern scheduler while preserving mission-relevant behavior.

\* Multiplayer for the new engine may use a new protocol unless original protocol compatibility is explicitly added later.



Compatibility is required at the behavior boundary.



Historical implementation similarity is not required.



\---



\# 4. Strict Non-Goals



The project must not begin by attempting to:



1\. Recreate the Arma 2 DirectX renderer.

2\. Reproduce Arma 2 source code exactly.

3\. Decompile the whole Arma 2 executable.

4\. Copy generated decompiler output into production code.

5\. Reintroduce historical renderer bottlenecks.

6\. Reintroduce historical memory, world-size, object-count, or draw-call limits.

7\. Preserve every engine bug.

8\. Support every Arma 2 addon immediately.

9\. Connect to original Arma 2 servers.

10\. Implement original network protocol compatibility.

11\. Mix Operation Arrowhead behavior into the initial Arma 2 target.

12\. Rewrite the entire existing codebase before producing a working Arma 2 vertical slice.

13\. Force a new directory structure before understanding the current source tree.

14\. Assume that Arma 2 should inherit directly from the current Arma 1 implementation.

15\. Assume that existing systems are badly structured merely because they are old.

16\. Refactor unrelated working code for aesthetic reasons.



\---



\# 5. Source-Aware Planning Rule



No agent may implement the architecture in this document literally without first mapping the actual repository.



The first engineering output must be an evidence-based architecture map.



Agents must inspect:



\* repository structure;

\* build targets;

\* libraries and executables;

\* renderer boundaries;

\* content-loading flow;

\* configuration systems;

\* script runtime;

\* simulation loop;

\* entity representation;

\* world representation;

\* animation systems;

\* physics systems;

\* AI systems;

\* test infrastructure;

\* current compatibility logic;

\* existing extension or plugin mechanisms;

\* existing registries, factories, interfaces, adapters, or service locators;

\* current Vulkan abstraction;

\* systems already moved to the GPU;

\* systems still inherited directly from the original code.



The plan must be adapted to these findings.



\---



\# 6. No Assumed Repository Layout



This document intentionally does not prescribe exact paths such as:



```text

src/engine/

src/compatibility/arma2/

src/rv/common/

```



Those names may be suitable, but the agents must not assume they are.



After inspecting the source, the architecture agent must propose one of the following:



\* use the existing project structure;

\* extend existing subsystem boundaries;

\* add new module targets;

\* introduce a small compatibility directory;

\* introduce interfaces around existing systems;

\* use compile-time feature targets;

\* use runtime-loaded modules;

\* use policy objects;

\* use registries and factories;

\* use another design that fits the repository better.



The proposal must explain why it fits the existing codebase.



\---



\# 7. Required Discovery Deliverables



Before substantial compatibility implementation begins, produce:



\## 7.1 Source map



A document identifying:



\* major source areas;

\* major build targets;

\* ownership of each subsystem;

\* entry points;

\* initialization order;

\* simulation update order;

\* render submission flow;

\* asset-loading flow;

\* script execution flow;

\* world-loading flow.



\## 7.2 Dependency map



A dependency graph showing:



\* which subsystems depend on which;

\* where renderer-specific code enters simulation or content systems;

\* where game-specific types appear in generic systems;

\* where likely compatibility seams already exist;

\* potential circular dependencies.



\## 7.3 Compatibility contamination map



Identify current code that appears to combine:



\* generic engine behavior;

\* shared Real Virtuality behavior;

\* Arma 1-specific behavior;

\* renderer behavior;

\* content parsing;

\* simulation semantics.



Do not move this code yet.



Classify it and record it.



\## 7.4 Existing abstraction inventory



List all reusable mechanisms already present:



\* interfaces;

\* factories;

\* registries;

\* abstract base classes;

\* templates;

\* policy types;

\* plugin APIs;

\* virtual filesystems;

\* parser interfaces;

\* serialization layers;

\* shader/material abstractions;

\* compatibility switches;

\* build flags.



\## 7.5 Test and build inventory



Record:



\* supported compilers;

\* supported platforms;

\* current CI;

\* test frameworks;

\* integration tests;

\* asset fixtures;

\* benchmark tools;

\* debug overlays;

\* profiling systems;

\* sanitizer support;

\* reproducible launch commands.



\## 7.6 Change-risk map



Classify systems by modification risk:



```text

LOW

MEDIUM

HIGH

CRITICAL

```



Critical systems may include parts of the engine that are already heavily modernized or difficult to regression-test.



\---



\# 8. Architecture Decision Process



After discovery, the architecture agent must write an architecture proposal.



It must answer:



1\. What is the smallest clean boundary between the modern engine and compatibility behavior?

2\. How can current Arma 1 behavior be wrapped without rewriting it?

3\. How can Arma 2 behavior be added without depending directly on Arma 1-specific implementation?

4\. Which code is genuinely shared?

5\. Which shared code should remain duplicated temporarily until common behavior is proven?

6\. How will the selected compatibility target be chosen?

7\. How will compatibility-specific parsers be registered?

8\. How will simulation differences be represented?

9\. How will script-command differences be represented?

10\. How will compatibility modules be disabled at build time?

11\. How will the engine be tested without one or both compatibility targets?

12\. How will a compatibility target eventually be removed?

13\. How will the architecture preserve existing Vulkan systems?

14\. Which existing patterns in the repository should be reused?



The architecture proposal must include at least two alternatives and explain why one is preferred.



No major modular extraction should begin before this review.



\---



\# 9. Architectural Invariants



Regardless of the final implementation mechanism, the following invariants are mandatory.



\## 9.1 Generic engine code must not depend on Arma 1 or Arma 2 implementations



The modern renderer, resource system, job system, generic animation runtime, generic world runtime, and generic simulation infrastructure must not require a specific compatibility target.



\## 9.2 Arma 2 must not require Arma 1 compatibility to be enabled



Shared code may be extracted, but the Arma 2 target must eventually build and run without the Arma 1 compatibility target.



\## 9.3 Compatibility-specific data must be translated before reaching generic runtime systems



The renderer should not receive raw Arma 2 material identities.



The generic animation runtime should not need to parse Arma 2 configuration.



The generic entity runtime should not load PBOs.



\## 9.4 Runtime hot paths must not contain scattered version checks



Avoid repeated logic such as:



```cpp

if (gameVersion == Arma2)

{

&#x20;   // special behavior

}

```



Compatibility selection should resolve concrete implementations before hot-path execution wherever practical.



\## 9.5 Compatibility behavior must have an owner



Every compatibility-specific behavior must clearly belong to:



\* Arma 1;

\* Arma 2;

\* shared Real Virtuality compatibility;

\* native modern behavior.



\## 9.6 Modules must be removable



Disabling one compatibility target must not require redesigning unrelated systems.



\## 9.7 Existing modern systems must remain authoritative



Compatibility modules may provide data and behavior policies.



They may not take ownership of the Vulkan renderer or replace working GPU systems with legacy implementations.



\---



\# 10. Compatibility Mechanisms



The agents must choose mechanisms that fit the real source.



Acceptable mechanisms may include:



\* interfaces;

\* registries;

\* factories;

\* policy objects;

\* strategy objects;

\* adapters;

\* service providers;

\* abstract data translators;

\* build targets;

\* static modules;

\* dynamic plugins;

\* compile-time feature flags;

\* table-driven behavior;

\* generated command registries;

\* versioned parser implementations.



No single mechanism is mandatory.



The chosen design should minimize:



\* invasive changes;

\* duplication;

\* version checks;

\* coupling;

\* runtime overhead;

\* migration risk.



\---



\# 11. Compatibility Target Selection



The engine must eventually support explicit selection of a compatibility target.



The exact API and command-line format should follow existing project conventions.



Conceptually:



```text

Start engine

&#x20;   ↓

Select compatibility target

&#x20;   ↓

Register target-specific providers

&#x20;   ↓

Freeze runtime services

&#x20;   ↓

Mount content

&#x20;   ↓

Load configuration

&#x20;   ↓

Create world

&#x20;   ↓

Start simulation

```



The selected target should provide or register the implementations needed for:



\* archive handling;

\* config semantics;

\* model formats;

\* material translation;

\* animation semantics;

\* script command set;

\* script scheduling;

\* world loading;

\* simulation policies;

\* AI policies;

\* diagnostics.



If the current codebase already has a suitable initialization or factory system, reuse it.



\---



\# 12. Build-Time Modularity



Each compatibility target must eventually be independently enabled or disabled.



The exact build-system syntax must be derived from the current project.



Required build combinations:



1\. Existing engine with Arma 1 compatibility.

2\. Existing engine with Arma 2 compatibility.

3\. Existing engine with both.

4\. Generic engine or tool targets without either compatibility target, where technically possible.

5\. Tests for Arma 2 with Arma 1 disabled.

6\. Tests for Arma 1 with Arma 2 disabled.



The build should reveal hidden dependencies.



If the current repository architecture cannot immediately support these combinations, create a staged migration plan rather than forcing them all at once.



\---



\# 13. Gradual Migration Strategy



The current code is likely to contain behavior inherited from the existing game implementation.



Do not assume that all current code belongs in an Arma 1 module.



Do not assume that all current code belongs in the generic engine.



For each subsystem touched by Arma 2 work:



1\. Inspect the current implementation.

2\. Identify generic mechanisms.

3\. Identify Arma 1-specific semantics.

4\. Identify shared Real Virtuality semantics.

5\. Preserve existing behavior with tests.

6\. Introduce the smallest useful compatibility seam.

7\. Wrap current behavior where possible.

8\. Add the Arma 2 implementation alongside it.

9\. Extract shared code only when supported by evidence.

10\. Verify independent builds.



The migration should follow a strangler pattern:



```text

Existing mixed subsystem

&#x20;       ↓

Small compatibility interface

&#x20;       ↓

Existing behavior wrapped as current target

&#x20;       ↓

New Arma 2 implementation added

&#x20;       ↓

Shared behavior extracted only where useful

```



Avoid mass relocation of files before behavior is understood.



\---



\# 14. Compatibility Contracts



Before implementing each subsystem, define its observable contract.



Each contract must specify:



\* inputs;

\* outputs;

\* state;

\* timing;

\* errors;

\* edge cases;

\* exactness requirements;

\* tolerance requirements;

\* performance budget;

\* test method.



Example categories:



\## Exact contracts



\* config inheritance;

\* file parsing;

\* script types;

\* script parsing;

\* path resolution;

\* property lookup;

\* model selection names;

\* archive contents;

\* class dependency order.



\## Tolerant contracts



\* ballistics;

\* vehicle handling;

\* animation blending;

\* AI timing;

\* visual materials;

\* particles;

\* audio;

\* physics contacts.



Compatibility is not considered implemented until the contract has a reproducible test.



\---



\# 15. Intermediate Representations



Version-specific content should be translated into engine-neutral data before reaching generic runtime systems.



The actual types must be designed after inspecting existing asset and runtime structures.



Likely concepts include:



\* model representation;

\* material representation;

\* world representation;

\* config representation;

\* skeleton representation;

\* animation representation;

\* collision representation;

\* entity-definition representation.



Do not create new intermediate representations if the engine already has appropriate ones.



Prefer adapting into existing modern asset types when those types can represent the required semantics cleanly.



Create new IR only when it provides a clear compatibility boundary.



\---



\# 16. Auto RE Agent Workstream



Auto RE Agent is a formal project tool, not an optional afterthought.



It should be used for bounded, evidence-driven research into undocumented Arma 2 behavior.



It must not be used as an automatic whole-engine conversion system.



\---



\# 17. Auto RE Agent Infrastructure Phase



Create a dedicated research environment separate from production code.



The exact paths and tooling integration should follow the repository’s conventions.



Required components:



1\. A documented Auto RE Agent installation procedure.

2\. A documented Ghidra project setup.

3\. A repeatable way to load the legally owned Arma 2 executable.

4\. A repeatable way to provide relevant open-source code as contextual reference.

5\. A versioned Auto RE Agent configuration.

6\. A versioned prompt library.

7\. A research-ticket template.

8\. A report-output format.

9\. A symbol and function-matching database.

10\. A clean handoff process from research to implementation.

11\. A clear rule preventing generated code from being committed directly.

12\. A way to reproduce research findings.



The research environment should store:



\* executable hashes;

\* game version information;

\* function addresses;

\* inferred names;

\* call graphs;

\* string references;

\* class and vtable hypotheses;

\* structure hypotheses;

\* confidence values;

\* links to behavioral tests;

\* agent reports.



It must not store proprietary executables in the repository.



\---



\# 18. Auto RE Agent Pilot



Before using Auto RE Agent for difficult systems, run a small pilot.



The pilot target must:



\* be bounded;

\* have measurable input and output;

\* have a likely related implementation in the available open source;

\* not involve the renderer;

\* not require dozens of unknown dependencies;

\* be independently testable in the original game.



Suitable pilot categories include:



\* one config-resolution rule;

\* one animation-source rule;

\* one script-command edge case;

\* one simple data conversion function;

\* one event-ordering rule.



The pilot must prove the full workflow:



```text

Compatibility question

&#x20;       ↓

Public-source and codebase research

&#x20;       ↓

Original-game behavioral test

&#x20;       ↓

Ghidra analysis

&#x20;       ↓

Auto RE Agent investigation

&#x20;       ↓

Evidence report

&#x20;       ↓

Behavioral specification

&#x20;       ↓

Independent clean implementation

&#x20;       ↓

Regression test

```



The pilot is successful only if another agent can implement the behavior from the report without copying generated pseudocode.



\---



\# 19. When Auto RE Agent Must Be Used



Use Auto RE Agent when:



\* existing source does not explain Arma 2 behavior;

\* public documentation is incomplete;

\* original-game tests reveal behavior but not the rules;

\* binary evidence can resolve a specific compatibility question;

\* a function or group of functions has a bounded responsibility;

\* data structures or call relationships need clarification.



Good research targets:



\* config edge cases;

\* animation source semantics;

\* damage formulas;

\* component damage;

\* script scheduler behavior;

\* script locality;

\* event ordering;

\* vehicle controller behavior;

\* AI state transitions;

\* unknown model or world fields;

\* object initialization order;

\* save or mission-state semantics.



\---



\# 20. When Auto RE Agent Must Not Be Used



Do not use it to:



\* recreate the original renderer;

\* recreate old shaders;

\* recreate DirectX state management;

\* rebuild obsolete memory allocators;

\* rebuild platform-specific code that is irrelevant;

\* generate a full Arma 2 source tree;

\* replace working modern engine systems;

\* answer questions already covered by source, documentation, or tests;

\* produce code for direct inclusion without independent review.



\---



\# 21. Auto RE Research Ticket



Every Auto RE task must begin with:



```markdown

\# Research Question



\## Compatibility behavior

What exact behavior is unknown?



\## Why the answer matters

Which content or test is blocked?



\## Existing evidence

What source code, documentation, and tests have been checked?



\## Observable inputs

What values enter the behavior?



\## Observable outputs

What can be measured?



\## Original-game test

What minimal test reproduces the behavior?



\## Candidate binary area

Functions, strings, classes, structures, vtables, or call sites.



\## Expected deliverable

Behavioral specification, evidence, confidence, and proposed tests.



\## Explicit exclusions

Systems that must not be investigated or copied.

```



\---



\# 22. Auto RE Agent Output Requirements



The research agent must produce:



1\. Research question.

2\. Investigated executable version and hash.

3\. Relevant functions and addresses.

4\. Relevant strings and cross-references.

5\. Relevant call relationships.

6\. Suspected inputs and outputs.

7\. Suspected state and side effects.

8\. Edge cases.

9\. Confidence level.

10\. Alternative interpretations.

11\. Behavioral specification.

12\. Proposed regression tests.

13\. Open questions.

14\. Evidence that the result matches original-game observations.



Generated candidate code may be stored in an isolated research workspace.



It must not be treated as production code.



\---



\# 23. Research and Implementation Separation



Where practical, use separate agents:



\## Binary Research Agent



Receives:



\* executable;

\* Ghidra project;

\* Auto RE Agent;

\* open-source context;

\* oracle test results.



Produces:



\* evidence;

\* specification;

\* confidence;

\* tests.



\## Compatibility Implementation Agent



Receives:



\* behavioral specification;

\* tests;

\* current source architecture;

\* approved interfaces.



Produces:



\* clean implementation;

\* unit tests;

\* integration tests;

\* documentation.



The implementation agent should not rely on copied decompiler expression.



This separation reduces accidental contamination and encourages architecture-consistent code.



\---



\# 24. Agent Roles



The actual number of agents may vary, but responsibilities must remain distinct.



\## Architecture Agent



\* maps the current architecture;

\* proposes compatibility seams;

\* reviews dependency direction;

\* approves shared abstractions;

\* prevents unnecessary refactoring;

\* prevents Arma 2 from depending on Arma 1-specific code;

\* reviews removability.



\## Source Discovery Agent



\* maps the repository;

\* identifies existing patterns;

\* finds related systems;

\* finds tests;

\* records initialization and update flow;

\* prevents duplicate implementations.



\## Build and CI Agent



\* creates reproducible builds;

\* maintains build combinations;

\* adds sanitizer and warning configurations;

\* verifies module removal;

\* maintains baseline builds.



\## Migration Agent



\* wraps existing behavior;

\* introduces minimal seams;

\* extracts shared behavior when justified;

\* preserves current functionality;

\* avoids mass rewrites.



\## Format Research Agent



\* investigates versions;

\* documents binary layouts;

\* creates fixtures;

\* records unknown fields;

\* avoids invented semantics.



\## Asset Pipeline Agent



\* implements validation;

\* translates legacy content;

\* builds inspection tools;

\* integrates with existing asset systems;

\* avoids direct parser-to-renderer coupling.



\## Configuration Agent



\* handles addon discovery;

\* patch ordering;

\* inheritance;

\* class resolution;

\* property provenance;

\* configuration diagnostics.



\## Scripting Agent



\* command registry;

\* parser compatibility;

\* runtime behavior;

\* scheduler;

\* namespaces;

\* event handlers;

\* locality;

\* errors.



\## Animation Agent



\* skeletons;

\* named selections;

\* animation sources;

\* procedural controls;

\* weapon attachments;

\* GPU animation integration.



\## Simulation Agent



\* entity creation;

\* inventory;

\* weapons;

\* damage;

\* vehicles;

\* mission state;

\* triggers;

\* waypoints.



\## AI Agent



\* navigation;

\* perception;

\* formations;

\* targeting;

\* combat decisions;

\* group behavior;

\* deterministic testing.



\## Renderer Integration Agent



\* translates imported materials;

\* integrates imported geometry;

\* preserves GPU systems;

\* protects HDR and Vulkan architecture;

\* rejects legacy renderer leakage.



\## Oracle and Testing Agent



\* builds minimal original-game tests;

\* normalizes results;

\* defines tolerances;

\* compares behavior;

\* maintains golden results.



\## Binary Research Agent



\* operates Ghidra and Auto RE Agent;

\* investigates bounded questions;

\* produces behavioral specifications;

\* records confidence and evidence.



\## Performance Agent



\* measures CPU and GPU cost;

\* finds stalls;

\* tracks streaming;

\* detects compatibility overhead;

\* enforces budgets.



\---



\# 25. Standard Agent Task Format



Every task must include:



```markdown

\# Task



\## Objective

One measurable result.



\## Source evidence

Files, symbols, systems, and tests already inspected.



\## Architectural owner

Which current subsystem or proposed module owns the result?



\## Allowed changes

Exact source areas the agent may modify.



\## Forbidden changes

Systems that must remain untouched.



\## Required behavior

Observable expected result.



\## Compatibility target

Arma 1, Arma 2, shared, or modern native behavior.



\## Required tests

Unit, integration, oracle, removability, and performance tests.



\## Acceptance criteria

Objective pass or fail conditions.



\## Documentation

Required updates.



\## Unknowns

Questions that must be investigated rather than guessed.

```



Agents must not receive vague tasks such as:



> Add Arma 2 support.



Tasks should be bounded, such as:



> Identify how the current config system resolves inherited properties, propose the smallest seam for version-specific resolution, and add a test proving that current behavior remains unchanged.



\---



\# 26. Phase 0 — Baseline Preservation



\## Objective



Capture the current state before compatibility work changes anything.



\## Required work



\* identify the baseline commit;

\* produce reproducible builds;

\* record supported platforms;

\* record current compiler and dependency versions;

\* run existing tests;

\* capture representative game scenes;

\* record CPU performance;

\* record GPU performance;

\* record memory usage;

\* record asset-loading behavior;

\* record current Arma 1 compatibility;

\* record current Vulkan capabilities;

\* record existing GPU-based systems;

\* record known bugs;

\* create a one-command baseline test.



\## Exit criteria



\* a clean checkout builds;

\* the current game runs;

\* representative missions run;

\* performance data is stored;

\* regression scenes exist;

\* current behavior can be compared after changes.



\---



\# 27. Phase 1 — Source and Architecture Discovery



\## Objective



Understand the real codebase without changing its architecture prematurely.



\## Required outputs



\* source map;

\* dependency graph;

\* initialization map;

\* update-loop map;

\* rendering-flow map;

\* asset-flow map;

\* scripting-flow map;

\* compatibility contamination map;

\* test inventory;

\* build inventory;

\* risk map;

\* architecture alternatives.



\## Exit criteria



\* agents can explain how the engine currently starts;

\* agents can explain how content reaches the renderer;

\* agents can identify where game-specific behavior currently lives;

\* agents can identify at least one safe compatibility seam for the first vertical slice;

\* no major architecture assumptions remain undocumented.



\---



\# 28. Phase 2 — Architecture Proposal and Prototype Seam



\## Objective



Prove a minimal compatibility boundary before broader extraction.



\## Required work



1\. Propose at least two modular designs.

2\. Select one based on the existing source.

3\. Introduce the smallest compatibility selection mechanism.

4\. Wrap one current behavior as the existing target.

5\. Add one empty or stub Arma 2 implementation.

6\. Verify current behavior remains unchanged.

7\. Verify the new implementation can be selected.

8\. Verify the core does not need direct game-version checks for the chosen behavior.



\## Exit criteria



\* the architecture works for one real subsystem;

\* current behavior is preserved;

\* the design is documented;

\* the seam is accepted for extension;

\* no broad migration has been started unnecessarily.



\---



\# 29. Phase 3 — Compatibility Build and Removal Framework



\## Objective



Make compatibility ownership testable.



\## Required work



\* add build options or target selection consistent with the project;

\* allow Arma 2 work to compile without depending on Arma 1-specific implementation;

\* add dependency checks;

\* add compatibility ownership reporting;

\* add runtime capability reporting;

\* add module-removal tests where feasible;

\* add structured diagnostics.



\## Exit criteria



\* hidden dependencies are detectable;

\* compatibility capabilities can be reported;

\* unrelated targets can be disabled;

\* architecture violations fail CI or an equivalent validation step.



\---



\# 30. Phase 4 — Auto RE Agent Infrastructure



\## Objective



Create a repeatable binary-research workflow before hard compatibility questions appear.



\## Required work



\* install and document Auto RE Agent;

\* install and document the Ghidra integration;

\* create the Arma 2 analysis project;

\* record executable version and hash;

\* define research workspace rules;

\* create prompt templates;

\* create research ticket templates;

\* create report templates;

\* create symbol and function mapping storage;

\* define how open-source context is provided;

\* define implementation handoff;

\* run the pilot investigation.



\## Exit criteria



\* another agent can reproduce the setup;

\* the pilot produces a usable behavioral specification;

\* a separate agent implements and tests the pilot behavior;

\* no generated decompiler code enters production.



\---



\# 31. Phase 5 — Content Mounting and Addon Discovery



\## Objective



Allow the engine to inspect Arma 2 content without creating runtime entities.



The exact reuse of existing archive and filesystem systems must be decided after source inspection.



\## Required behavior



\* select external game-data roots;

\* enumerate archives;

\* normalize logical paths;

\* resolve archive prefixes;

\* identify addons;

\* build dependency information;

\* report missing dependencies;

\* detect duplicates;

\* detect malformed input;

\* avoid path traversal;

\* produce deterministic manifests.



\## Exit criteria



\* selected Arma 2 content can be mounted;

\* addons and files can be listed;

\* no model or world loading is required yet;

\* diagnostics identify unsupported or malformed data.



\---



\# 32. Phase 6 — Configuration Compatibility



\## Objective



Resolve one complete Arma 2 entity dependency chain.



Agents must first understand the existing configuration system and reuse it where suitable.



\## Required progression



1\. Addon declarations and dependencies.

2\. Vehicle or entity classes.

3\. Weapon classes.

4\. Magazine classes.

5\. Ammunition classes.

6\. Model definitions.

7\. Skeleton definitions.

8\. Animation definitions.

9\. World definitions.

10\. Surface and material definitions.



\## Required behavior



\* class declarations;

\* inheritance;

\* nested classes;

\* arrays;

\* strings;

\* numeric values;

\* patch ordering;

\* deletion or replacement behavior;

\* property provenance;

\* source locations;

\* deterministic dumps;

\* unknown-property preservation.



\## Exit criteria



The engine can explain and resolve:



\* one soldier or entity class;

\* one weapon;

\* one magazine;

\* one ammunition class;

\* model and animation references;

\* the origin of inherited values.



\---



\# 33. Phase 7 — Static Model and Material Vertical Slice



\## Objective



Render one static Arma 2 object through the modern Vulkan path.



Agents must inspect current model, mesh, material, texture, and resource structures before introducing new representations.



\## Required work



\* detect supported model version;

\* parse one required render LOD;

\* resolve material references;

\* resolve texture references;

\* translate material semantics;

\* create modern GPU resources;

\* preserve existing culling;

\* preserve HDR;

\* preserve resource lifetime rules;

\* expose debug information.



\## Exit criteria



One Arma 2 object:



\* loads from external data;

\* appears at correct scale and orientation;

\* uses translated materials;

\* participates in the existing Vulkan renderer;

\* uses current GPU culling where applicable;

\* does not require legacy renderer code.



\---



\# 34. Phase 8 — Model Semantics and Animation



\## Objective



Load one animated Arma 2 character or mechanical object.



\## Required behavior



\* named selections;

\* memory points;

\* proxies;

\* skeleton hierarchy;

\* animation-source values;

\* phase calculation;

\* axes;

\* procedural controls;

\* attachments;

\* blending;

\* GPU skinning integration.



\## Recommended progression



1\. Simple animated object.

2\. Door or rotating component.

3\. Turret.

4\. Weapon recoil.

5\. Character idle.

6\. Character movement.

7\. Character aiming.

8\. Weapon attachment.



\## Exit criteria



A selected animated asset:



\* resolves its skeleton;

\* plays required animations;

\* exposes compatibility semantics;

\* uses modern animation and GPU systems.



\---



\# 35. Phase 9 — Script and Mission Runtime



\## Objective



Run a minimal Arma 2 test mission.



Agents must inspect the current script runtime before deciding whether to extend, adapt, wrap, or partially replace parts of it.



\## Priority behavior



\* values and variables;

\* arrays and strings;

\* expressions;

\* control flow;

\* script execution;

\* entity creation;

\* transforms;

\* groups;

\* waypoints;

\* triggers;

\* event handlers;

\* weapon and inventory basics;

\* animation commands;

\* logging;

\* mission completion.



\## Scheduler behavior



Test:



\* scheduled execution;

\* unscheduled execution;

\* sleep;

\* spawned scripts;

\* event handlers;

\* trigger execution;

\* frame boundaries;

\* errors;

\* termination;

\* time scaling.



\## Exit criteria



A test mission can:



1\. create entities;

2\. group them;

3\. assign waypoints;

4\. run triggers;

5\. execute events;

6\. update an objective;

7\. end successfully.



\---



\# 36. Phase 10 — World and Terrain



\## Objective



Load a bounded region of an Arma 2 world.



Agents must inspect the existing world, terrain, streaming, collision, and navigation systems before choosing translation structures.



\## Required progression



\* world metadata;

\* dimensions;

\* height data;

\* surface data;

\* object placements;

\* materials;

\* roads;

\* locations;

\* environment settings;

\* vegetation definitions;

\* collision;

\* navigation representation.



\## Modernization rule



Legacy world data should be translated into existing or improved modern systems:



\* GPU terrain;

\* streaming cells;

\* indirect object rendering;

\* modern vegetation;

\* modern collision;

\* modern navigation.



\## Exit criteria



A small region has:



\* correct terrain shape;

\* correct coordinates;

\* placed objects;

\* collision;

\* surface lookup;

\* one road;

\* stable streaming.



\---



\# 37. Phase 11 — Weapons, Ballistics, and Damage



\## Objective



Support a complete infantry combat loop.



\## Required progression



1\. Weapon attachment.

2\. Magazine state.

3\. Fire modes.

4\. Muzzle points.

5\. Projectile creation.

6\. Velocity.

7\. Gravity.

8\. Drag or friction.

9\. Collision.

10\. Hit effects.

11\. Basic damage.

12\. Component damage.

13\. Explosions.

14\. Script events.



\## Use of Auto RE Agent



Auto RE Agent should be used for undocumented formulas or ordering rules only after:



\* current source is checked;

\* public behavior is checked;

\* original-game oracle tests are created.



\## Exit criteria



Two entities can:



\* carry weapons;

\* fire;

\* produce approximately compatible trajectories;

\* receive damage;

\* die;

\* trigger script events.



\---



\# 38. Phase 12 — Vehicles and Turrets



\## Objective



Operate one simple Arma 2 vehicle.



\## Recommended progression



1\. Static weapon platform.

2\. Wheeled vehicle.

3\. Tracked vehicle.

4\. Helicopter.

5\. Aircraft.

6\. Boat.



\## Required behavior



\* config resolution;

\* crew positions;

\* proxy placement;

\* controls;

\* engine state;

\* wheels or tracks;

\* suspension;

\* turret hierarchy;

\* mounted weapons;

\* damage components;

\* lights;

\* sounds;

\* animations;

\* scripts.



\## Exit criteria



A selected vehicle can:



\* spawn;

\* accept a driver;

\* move;

\* steer;

\* fire;

\* take damage;

\* allow exit.



\---



\# 39. Phase 13 — AI Compatibility



\## Objective



Support a small mission-relevant combat scenario.



\## Required progression



1\. Navigation queries.

2\. Waypoint execution.

3\. Formations.

4\. Perception.

5\. Target selection.

6\. Fire decisions.

7\. Danger reaction.

8\. Cover behavior.

9\. Group coordination.

10\. Vehicle use.



\## Testing rule



Provide deterministic test mode where practical:



\* fixed time step;

\* seeded randomness;

\* controlled visibility;

\* reproducible scenario setup.



\## Auto RE Agent use



Use it for specific state transitions or undocumented calculations, not wholesale AI reconstruction.



\## Exit criteria



Two squads can:



\* follow waypoints;

\* maintain approximate formation;

\* detect enemies;

\* engage;

\* react to casualties;

\* complete mission logic;

\* produce reproducible test results in deterministic mode.



\---



\# 40. Phase 14 — Complete Mission Vertical Slice



\## Objective



Run one carefully selected Arma 2 mission from start to finish.



\## Mission selection criteria



\* mainly infantry;

\* limited vehicles;

\* modest terrain area;

\* minimal custom UI;

\* limited campaign dependencies;

\* clear completion condition;

\* useful script coverage.



\## Completion criteria



\* world loads;

\* player spawns;

\* scripts execute;

\* AI moves;

\* combat works;

\* objectives update;

\* mission ends;

\* unsupported features are reported;

\* performance is acceptable;

\* Arma 1 behavior remains intact.



\---



\# 41. Phase 15 — Compatibility Expansion



After the first mission works, prioritize new work using evidence:



\* mission blockers;

\* unsupported-command telemetry;

\* addon usage;

\* format frequency;

\* crash frequency;

\* performance impact;

\* implementation cost;

\* testability.



Possible later work:



\* more worlds;

\* more characters;

\* more vehicles;

\* aircraft;

\* campaigns;

\* advanced UI;

\* multiplayer;

\* mod compatibility;

\* Operation Arrowhead as a separate target;

\* native modern content pipeline.



\---



\# 42. Testing Strategy



Tests must be organized by ownership and behavior, using the existing test framework wherever possible.



Required categories:



\* current-game regressions;

\* generic engine tests;

\* shared compatibility tests;

\* Arma 1 tests;

\* Arma 2 tests;

\* parser tests;

\* malformed-input tests;

\* script tests;

\* simulation tests;

\* performance tests;

\* module-removal builds;

\* complete mission tests.



\---



\# 43. Behavioral Oracle



Use the original Arma 2 game as a controlled behavioral reference.



\## Workflow



1\. Create the smallest possible test.

2\. Remove unrelated variables.

3\. Fix random seeds where possible.

4\. Log structured results.

5\. Run multiple times.

6\. identify nondeterministic behavior.

7\. normalize results.

8\. run an equivalent test in the new engine.

9\. compare.

10\. document exactness or tolerance.



Store:



\* user-created test scripts;

\* normalized logs;

\* tested version hashes;

\* procedures;

\* expected results.



Do not store proprietary assets or executables in the source repository.



\---



\# 44. Golden and Tolerance Tests



Exact behavior should use exact comparisons.



Floating-point or timing behavior should use documented tolerances.



Each tolerance test must state:



\* expected value;

\* observed value;

\* tolerance type;

\* tolerance amount;

\* reason for tolerance;

\* number of reference runs;

\* compatibility importance.



Golden files must be deterministic and human-reviewable where possible.



Golden-file changes require an explanation.



\---



\# 45. Performance Rules



Compatibility must not undo Vulkan modernization.



Mandatory rules:



\* no unbounded compatibility scans per frame;

\* cache resolved config data;

\* index archive paths;

\* translate materials once;

\* cache converted models;

\* avoid repeated string lookup in hot paths;

\* intern identifiers where appropriate;

\* preserve asynchronous loading;

\* preserve GPU culling;

\* preserve GPU skinning;

\* preserve HDR;

\* preserve indirect rendering;

\* avoid compatibility-specific render-thread stalls;

\* resolve compatibility providers at startup where practical.



Track:



\* simulation CPU time;

\* script CPU time;

\* streaming CPU time;

\* render-thread time;

\* GPU time;

\* draw or indirect-command counts;

\* visible objects;

\* animated objects;

\* memory;

\* VRAM;

\* loading bandwidth;

\* shader stalls;

\* synchronization waits.



\---



\# 46. Diagnostics



Compatibility diagnostics must identify:



\* compatibility target;

\* addon or package;

\* file;

\* class or asset;

\* unsupported behavior;

\* fallback;

\* severity;

\* stable error identifier.



At mission shutdown, produce a summary such as:



```text

Unsupported script commands

Material translation fallbacks

Missing selections

Ignored config properties

Skipped objects

Unsupported animation sources

Unsupported simulation behavior

```



Diagnostics must be rate-limited and suitable for automated reports.



\---



\# 47. Documentation Requirements



Maintain:



\* project scope;

\* source architecture map;

\* compatibility architecture;

\* subsystem ownership;

\* dependency rules;

\* compatibility matrix;

\* known differences;

\* migration status;

\* testing guide;

\* Auto RE Agent setup;

\* reverse-engineering policy;

\* performance budgets;

\* legal boundaries;

\* decision records;

\* milestone status.



Every major shared-versus-specific decision must be documented.



\---



\# 48. Compatibility Matrix



Maintain a machine-readable feature matrix.



Example states:



```text

unsupported

planned

researching

experimental

partial

compatible

complete

blocked

```



Track independently:



\* archives;

\* configs;

\* models;

\* materials;

\* textures;

\* animations;

\* worlds;

\* scripting;

\* weapons;

\* damage;

\* vehicles;

\* AI;

\* missions;

\* multiplayer.



The matrix must distinguish Arma 1 and Arma 2.



\---



\# 49. Legal and Data Boundaries



Rules:



1\. Use a distinct unofficial project identity.

2\. Do not distribute proprietary game data.

3\. Do not commit proprietary executables.

4\. Do not commit extracted game assets.

5\. Keep installed game data external.

6\. Use generated fixtures where possible.

7\. Store hashes and behavioral results rather than proprietary binaries.

8\. Separate binary research from implementation.

9\. Do not paste decompiler output into production code.

10\. Review licensing and redistribution questions before public releases.



\---



\# 50. Pull Request Requirements



Every pull request must include:



1\. Problem statement.

2\. Source areas inspected.

3\. Architectural owner.

4\. Compatibility target.

5\. Technical approach.

6\. Existing abstraction reused.

7\. New abstraction introduced.

8\. Files changed.

9\. Tests added.

10\. Build combinations tested.

11\. Current-game regression status.

12\. Performance impact.

13\. Compatibility impact.

14\. Removal impact.

15\. Known limitations.

16\. Documentation changes.

17\. Rollback instructions.



Do not merge when:



\* the agent has not inspected related source;

\* the implementation assumes a structure not present in the repository;

\* engine code gains unnecessary game-version checks;

\* Arma 2 depends on Arma 1-specific code;

\* generated decompiler code is copied into production;

\* unrelated refactoring is included;

\* current behavior breaks;

\* performance regresses without approval;

\* module-removal builds fail;

\* proprietary data is committed.



\---



\# 51. Initial Task Sequence



The exact tasks must be refined after source discovery.



Recommended starting order:



1\. Capture baseline build and performance.

2\. Map the repository.

3\. Map the build targets.

4\. Map engine initialization.

5\. Map asset loading.

6\. Map rendering flow.

7\. Map script execution.

8\. Map configuration flow.

9\. Map entity and simulation flow.

10\. Identify current game-specific behavior.

11\. Identify existing extension patterns.

12\. Produce architecture alternatives.

13\. Prototype one compatibility seam.

14\. Wrap current behavior behind that seam.

15\. Add a stub Arma 2 provider.

16\. Add independent build validation.

17\. Add architecture dependency checks.

18\. Create compatibility diagnostics.

19\. Create compatibility capability reporting.

20\. Set up Auto RE Agent and Ghidra.

21\. Run the Auto RE pilot.

22\. Build external Arma 2 content mounting.

23\. Build deterministic content inspection.

24\. Resolve one configuration dependency chain.

25\. Load one static Arma 2 model.

26\. Translate one material.

27\. Render the first Arma 2 object.

28\. Load one animation.

29\. Run one minimal script.

30\. Build the first soldier vertical slice.



\---



\# 52. First Major Vertical Slice



The first major demonstration should prove both compatibility and modular architecture.



It must:



1\. Start using the Arma 2 compatibility target.

2\. Mount a user-selected Arma 2 installation.

3\. Discover required addon data.

4\. Resolve one entity class.

5\. Resolve one weapon chain.

6\. Load one character or representative model.

7\. Translate its materials.

8\. Resolve required selections and memory points.

9\. Load one animation.

10\. Attach one weapon or proxy.

11\. Create the entity through a script or mission definition.

12\. Place it in a simple test environment.

13\. Render it through Vulkan.

14\. Preserve HDR.

15\. Preserve existing GPU systems.

16\. Report unsupported features.

17\. Build without Arma 1-specific implementation enabled.

18\. Preserve current Arma 1 behavior in its own build.



\---



\# 53. Definition of Done



A compatibility feature is done only when:



\* the relevant source was inspected;

\* its owner is clear;

\* the implementation fits the real architecture;

\* current behavior is preserved;

\* Arma 2 does not depend on Arma 1-specific code;

\* observable behavior is documented;

\* exactness or tolerance is defined;

\* tests exist;

\* diagnostics exist;

\* removal is possible;

\* build combinations pass;

\* performance impact is measured;

\* documentation is updated;

\* compatibility matrix is updated;

\* no proprietary data is committed;

\* unknowns are stated honestly.



The following are not sufficient:



\* it compiles;

\* it looks correct once;

\* the agent believes it is compatible;

\* it only works when every compatibility target is enabled;

\* it requires scattered version checks;

\* it duplicates an entire subsystem without justification;

\* it copies reverse-engineered implementation expression.



\---



\# 54. Standing Instructions for All AI Agents



Before changing code:



1\. Inspect the real source.

2\. Search for existing related systems.

3\. Search for existing tests.

4\. Trace the relevant execution flow.

5\. Identify the current owner.

6\. Identify the smallest safe seam.

7\. State assumptions.

8\. State unknowns.

9\. Define observable acceptance criteria.

10\. Define how unrelated compatibility targets remain removable.



While changing code:



1\. Stay within the approved scope.

2\. Preserve current behavior.

3\. Avoid unrelated cleanup.

4\. Reuse existing abstractions when suitable.

5\. Introduce new abstractions only with justification.

6\. Validate external data.

7\. Add diagnostics.

8\. Add tests.

9\. Keep commits small.

10\. Avoid game-version checks in hot generic code.

11\. Do not make Arma 2 depend on Arma 1-specific implementation.

12\. Do not replace modern Vulkan systems with legacy designs.



After changing code:



1\. Build all affected targets.

2\. Run unit tests.

3\. Run compatibility tests.

4\. Run current-game regressions.

5\. Run module-removal builds.

6\. Measure performance where relevant.

7\. Update documentation.

8\. Update compatibility status.

9\. Report remaining uncertainty.

10\. Record follow-up work.



\---



\# 55. Final Standard



The project must not become:



> The current Arma 1 engine patched repeatedly until some Arma 2 content works.



It must become:



> A modern Vulkan engine whose existing architecture has been carefully extended with explicit, testable, and removable compatibility behavior.



The source code determines the implementation design.



The architecture plan determines the required properties.



The agents must not guess the source structure.



They must inspect it.



The modern engine owns technology.



Compatibility layers own legacy interpretation.



Auto RE Agent provides evidence for undocumented behavior.



It does not define the production architecture.



The renderer remains Vulkan.



Arma 1 support must be removable.



Arma 2 support must be removable.



Neither compatibility target may own the future of the engine.



