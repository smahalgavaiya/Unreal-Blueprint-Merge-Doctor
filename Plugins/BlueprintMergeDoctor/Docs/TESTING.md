# Testing

All fixtures are generated as transient `UBlueprint` objects. This makes tests repeatable and avoids committing opaque `.uasset` binaries merely to validate structural rules.

The `BlueprintMergeDoctor` automation prefix currently covers:

- variable added only in OURS;
- variable added only in THEIRS;
- independent variables added on both sides and their compiled defaults;
- identical variable added by both sides;
- the same variable modified differently;
- function added only in OURS;
- functions independently added and compiled;
- components independently added and compiled;
- the same inherited default property modified differently;
- combined merged Blueprint compilation;
- the exact four-safe-additions/one-conflict product flow;
- the same GUID-addressed graph node moved differently, including detailed node conflict reporting.

Run from an Unreal Editor session through **Tools → Test Automation**, or headlessly:

```powershell
& 'C:\Program Files\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  '<Repo>\BlueprintMergeDoctorHost.uproject' `
  -unattended -nop4 -NullRHI -nosplash `
  -ExecCmds='Automation RunTests BlueprintMergeDoctor' `
  -TestExit='Automation Test Queue Empty' -log
```

Review `Saved/Logs/BlueprintMergeDoctorHost.log`. Each analysis logs safe-action, conflict, and manual-review counts; conflicts include the three summarized states and the classification reason.

When adding a new automatic rule, add at least:

1. a positive case proving the intended mutation and a clean compile;
2. a same-identity divergent case proving no automatic action is emitted;
3. a failure-path case if the Unreal API can reject the mutation.
