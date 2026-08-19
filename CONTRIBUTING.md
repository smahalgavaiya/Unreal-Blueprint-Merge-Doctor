# Contributing

Thank you for helping make Blueprint Merge Doctor safer.

## Before opening an issue

- Reproduce the problem on copies of the affected assets.
- Include the Unreal Engine version, operating system, Blueprint parent class, expected classification, and actual classification.
- Do not upload proprietary `.uasset` files unless you are authorized to share them.
- Prefer a minimal reproduction project or exact asset-construction steps.

## Pull requests

Automatic merge behavior must remain deterministic. A pull request that adds or broadens an automatic rule should include:

1. a positive transient automation case;
2. a divergent same-identity case proving that no unsafe action is emitted;
3. a clean resulting Blueprint compile;
4. an explanation of the identity and safety assumptions.

Keep engine-version-sensitive editor API calls inside `FBlueprintMergeEngineAdapter` when practical. Do not add raw `.uasset` byte patching or automatic semantic conflict guesses.

Run the `BlueprintMergeDoctor` automation prefix before submitting. Commands are documented in [Plugins/BlueprintMergeDoctor/Docs/TESTING.md](Plugins/BlueprintMergeDoctor/Docs/TESTING.md).
