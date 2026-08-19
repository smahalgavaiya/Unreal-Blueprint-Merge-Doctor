# Blueprint Merge Doctor v0.1.0-beta.1

This first public beta demonstrates conservative three-way Blueprint merging in Unreal Engine.

## Highlights

- Select BASE, OURS, and THEIRS manually in the Unreal Editor.
- Inspect structured variable, graph, node, pin/link, component, interface, and default-property differences.
- Automatically copy only deterministic, non-overlapping whole additions.
- Always create a new asset from BASE; the three inputs are never overwritten.
- Compile the result before saving and isolate all unresolved changes for manual review.

## Verified scenario

The included automation suite proves a merge containing:

- `Stamina = 100` and `ConsumeStamina()` from OURS;
- `Ammo = 30` and `PerformAttack()` from THEIRS;
- divergent `MaxHealth` values classified as a conflict and left at BASE;
- a resulting Blueprint that compiles with zero errors.

## Validation

- 12/12 Blueprint Merge Doctor editor automation tests pass on UE 5.5.
- Standalone packaged-plugin validation is performed for each attached engine-specific archive.

## Beta boundaries

Existing-object edits, removals, and graph-node/pin/link modifications are never applied automatically. Git integration is not included. Test on copies, inspect every proposed change, and retain normal source-control backups.

See the repository README for installation, usage, testing commands, and the full safety model.
