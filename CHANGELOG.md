# Changelog

All notable changes to Blueprint Merge Doctor are documented here.

## [0.1.0-beta.1] - 2026-08-20

### Added

- Three-way BASE/OURS/THEIRS Blueprint analysis window under the Tools menu.
- Auditable classifications and explanations for variables, functions, macros, event graphs, nodes, pins, links, components, interfaces, and deterministic inherited defaults.
- Safe whole-addition merge rules for non-overlapping variables, functions, macro graphs, components, and interfaces.
- BASE-first, non-destructive asset creation with editor transactions.
- Compile-before-save validation and preservation of failed generated assets for inspection.
- Semantic graph digests that tolerate GUID regeneration while retaining GUID-addressed detail inspection.
- Twelve transient editor automation scenarios, including the exact four-safe-changes/one-conflict workflow.

### Known limitations

- Existing-object modifications and removals are inspection-only.
- Event graph and node/pin/link changes are never auto-applied.
- Git conflict-stage discovery is not included.
- Macro/interface paths, persistent save/reload, and compile-failure preservation need broader beta coverage.
