# Architecture

## Safety invariants

1. BASE, OURS, and THEIRS are read-only inputs.
2. Every persistent merge begins by duplicating BASE to a unique package name.
3. Only analyzer-produced safe actions reach the mutation layer.
4. Existing identities are not modified or removed in V1.
5. The new Blueprint is compiled with `EBlueprintCompileOptions::SkipSave`.
6. The package is saved only when compilation reports zero errors and the Blueprint status is not `BS_Error`.
7. A failure preserves the generated unsaved asset for inspection.
8. No code reads or patches raw `.uasset` bytes.

## Data flow

```text
UBlueprint inputs
    │
    ▼
FBlueprintMergeSnapshotBuilder
    read-only normalized snapshots
    │
    ▼
FBlueprintMergeAnalyzer
    rows + explanations + safe action plan
    │
    ├──────────────► Slate detail/preview UI
    │
    ▼
FBlueprintMergeService
    duplicate BASE → apply plan → compile → save on success
    │
    ▼
new merged UBlueprint
```

## Identity and normalization

- Variables, functions, macros, event graphs, and components are keyed by authored name.
- Interfaces are keyed by class path.
- Nodes and pins retain Unreal GUID keys for detailed lineage-aware comparison.
- A graph's semantic digest contains node class, position, pin type/default, and normalized link endpoints, but not raw GUID values. This avoids false conflicts when Unreal regenerates GUIDs while making side-by-side asset copies.
- Variable GUIDs are omitted from semantic equality, allowing the same independently-created variable addition to be recognized as identical. The selected source GUID is preserved in the output so cloned graph references can be refreshed.
- Component template comparison includes deterministic editable values. Branch-owned UObject references are detected separately and make the addition ineligible for automatic merge.

## Merge policy

The analyzer is deliberately stricter than a conventional text merge:

- Absent in BASE, present in one branch under a unique name: safe only for a supported whole-addition category.
- Absent in BASE, equivalent in both branches: safe for a supported category; apply one copy.
- Absent in BASE, different same-name definitions in both branches: conflict.
- Present in BASE and modified in one branch: reported, not applied.
- Present in BASE and modified differently in both branches: conflict.
- Removed in either branch: reported, not applied.

Safe actions carry an operation, source branch, object name/path, and explanation. The service does not re-infer policy.

## UE version boundary

`FBlueprintMergeEngineAdapter` contains calls most likely to change between UE5 minors:

- `FBlueprintEditorUtils::AddMemberVariable`;
- `FEdGraphUtilities::CloneGraph`;
- `USimpleConstructionScript` node creation and attachment;
- `FBlueprintEditorUtils::ImplementNewInterface`;
- `FKismetEditorUtilities::CompileBlueprint`.

Supporting another UE5 minor should start with compiling and running the full automation suite against that engine, then confining necessary conditionals to this adapter where possible.

## Future source-control integration

A future integration can sit before the current analyzer:

```text
source-control conflict
    → resolve BASE / OURS / THEIRS into temporary editor assets
    → invoke the existing analyzer and window
```

The comparison and mutation layers intentionally have no Git dependency.
