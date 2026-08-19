# Blueprint Merge Doctor

> **Public beta:** the deterministic merge core is working and covered by editor automation, but this release is intended for evaluation on copies of production assets. Review every proposed change and keep normal source-control backups.

Blueprint Merge Doctor is an Unreal Engine editor plugin for three-way Blueprint inspection and conservative merge creation. It compares a common ancestor (`BASE`), the current branch (`OURS`), and the incoming branch (`THEIRS`), then creates a **new** Blueprint containing only additions that can be copied deterministically.

The beta is validated against Unreal Engine 5.5 and uses editor/Blueprint APIs exclusively. It does not invoke Git, patch `.uasset` bytes, overwrite inputs, or use AI to choose a semantic resolution.

## Current prototype

Automatically applied:

- member variables added under non-overlapping names, including effective compiled defaults, metadata, flags, and GUIDs;
- whole function graphs added under non-overlapping names;
- whole macro graphs added under non-overlapping names;
- SCS components added under non-overlapping names when their parent is available and the template has no branch-owned object references;
- implemented interfaces added under non-overlapping class paths;
- equivalent additions made independently by both branches (one copy is applied).

Inspected but never automatically applied in V1:

- removals;
- modifications to an existing variable, function, component, interface, graph, node, pin, link, or inherited default property;
- event graph additions or edits;
- component additions with branch-local object references or unresolved parents;
- any same-name addition whose definitions differ.

For graph inspection, the snapshot includes graph ownership, function signatures, node classes, node GUIDs, positions, pin GUIDs/names/types/defaults, and normalized links. GUIDs are used for detailed lineage-aware inspection. Whole-graph equality uses a semantic digest that intentionally excludes raw GUID values because Unreal can regenerate GUIDs when users create side-by-side asset copies.

## Installation

### Use this repository as a host project

1. Install Unreal Engine 5.5.
2. Right-click `BlueprintMergeDoctorHost.uproject` and generate project files if desired.
3. Open `BlueprintMergeDoctorHost.uproject`; Unreal will build the editor plugin when required.

### Install into another project

1. Copy `Plugins/BlueprintMergeDoctor` into `<YourProject>/Plugins/BlueprintMergeDoctor`.
2. Regenerate project files for a C++ project, or allow Unreal to build the plugin on launch.
3. Enable **Blueprint Merge Doctor** under **Edit → Plugins** if it is not already enabled.
4. Restart the editor.

## Usage

1. Open **Tools → Blueprint Merge Doctor**.
2. Select the common ancestor as **BASE**.
3. Select the current branch version as **OURS**.
4. Select the incoming branch version as **THEIRS**.
5. Click **Analyze Merge**.
6. Review the summary, the detailed three-way table, the proposed safe additions, and every manual-review item.
7. Choose a new `/Game/...` package name and click **Create Merged Blueprint**.

The plugin duplicates BASE first. It applies the safe plan to that duplicate inside an editor transaction, compiles with saving disabled, and saves only after a clean compile. If an action or compile fails, no input is changed and the generated asset is left unsaved for inspection.

Unresolved conflicts do not disable creation. The output contains safe deterministic additions only; conflicting existing values remain at their BASE state.

## Classifications

| Classification | Meaning |
| --- | --- |
| `OURS ONLY` | Only OURS changed this identity. It is safe only when the row explicitly says so. |
| `THEIRS ONLY` | Only THEIRS changed this identity. It is safe only when the row explicitly says so. |
| `IDENTICAL CHANGE` | Both branches produced an equivalent state. Existing-object edits still remain manual in V1. |
| `SAFE NON-OVERLAPPING CHANGE` | Both branches add different identities in the same supported category. |
| `POTENTIAL CONFLICT` | Both branches affect the same identity differently, or one removes while the other modifies. |
| `UNSUPPORTED / MANUAL REVIEW` | The change is observable, but V1 has no sufficiently safe mutation rule. |

## Exact demonstrated flow

The `BlueprintMergeDoctor.EndToEnd.FourSafeChangesOneConflict` automation test constructs this scenario entirely in transient memory:

```text
BASE                       OURS                       THEIRS
Health = 100               + Stamina = 100           + Ammo = 30
MaxHealth = 100            + ConsumeStamina()        + PerformAttack()
                           MaxHealth = 120           MaxHealth = 150

Analyze
  ✓ 4 deterministic additions
  ⚠ 1 conflict: MaxHealth

Create merged Blueprint
  Health = 100
  MaxHealth = 100           (BASE retained; unresolved)
  Stamina = 100
  Ammo = 30
  ConsumeStamina()
  PerformAttack()
  ✓ compiles with zero errors
```

## Build and tests

From PowerShell with UE 5.5 installed in the default launcher location:

```powershell
& 'C:\Program Files\Epic Games\UE_5.5\Engine\Build\BatchFiles\Build.bat' `
  UnrealEditor Win64 Development `
  -Project="$PWD\BlueprintMergeDoctorHost.uproject" `
  -WaitMutex -NoHotReloadFromIDE

& 'C:\Program Files\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  "$PWD\BlueprintMergeDoctorHost.uproject" `
  -unattended -nop4 -NullRHI -nosplash `
  -ExecCmds='Automation RunTests BlueprintMergeDoctor' `
  -TestExit='Automation Test Queue Empty' -log
```

The automation suite creates transient Blueprints and never writes test output into `/Game`. The repository also includes four optional, manually created example assets under `Content/Blueprints` for trying the documented BASE/OURS/THEIRS flow in the editor. See [testing notes](Plugins/BlueprintMergeDoctor/Docs/TESTING.md).

## Architecture

Detection, policy, mutation, and UI are separate:

- `BlueprintMergeSnapshot.*` performs read-only Blueprint introspection and normalization.
- `BlueprintMergeAnalyzer.*` owns three-way comparison, classifications, explanations, and safe action planning.
- `BlueprintMergeEngineAdapter.*` isolates UE minor-version-sensitive Blueprint/Kismet/SCS calls.
- `BlueprintMergeService.*` duplicates BASE, applies the plan, compiles, and conditionally saves.
- `SBlueprintMergeDoctorWindow.*` contains Slate presentation and user interaction only.
- `Tests/BlueprintMergeDoctorTests.cpp` proves the merge rules with generated transient assets.

More detail is in [ARCHITECTURE.md](Plugins/BlueprintMergeDoctor/Docs/ARCHITECTURE.md).

## Known V1 boundaries

- Three assets are selected manually; there is no Git CLI dependency or automatic conflict-stage discovery.
- Existing graph bodies are inspection-only. The plugin does not splice nodes or choose between pin/link edits.
- Default-property detection is limited to deterministic value properties. Object/interface/delegate references are excluded from generic default comparison.
- Safe additions may still fail Unreal validation. A failed action or compile prevents saving and leaves the new asset available for diagnosis.
- Inputs should represent three versions of the same logical Blueprint and must have the same parent class and Blueprint type.

These boundaries are deliberate. Expanding automatic behavior should require a new rule with a deterministic identity model and an editor automation test.

## Feedback and security

- Report reproducible bugs through [GitHub Issues](https://github.com/smahalgavaiya/Unreal-Blueprint-Merge-Doctor/issues).
- For suspected security problems, follow [SECURITY.md](SECURITY.md) instead of opening a public issue.
- Contribution expectations are described in [CONTRIBUTING.md](CONTRIBUTING.md).

## License

Blueprint Merge Doctor is available under the [MIT License](LICENSE).

