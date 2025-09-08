## Summary
Briefly describe the change.

## Type of Change
- [ ] Feature
- [ ] Bug Fix
- [ ] Refactor / Cleanup
- [ ] Docs / Meta
- [ ] Build / CI

## Checklist
- [ ] CHANGELOG: Added entry under Unreleased (or confirmed not needed for trivial docs/build change)
- [ ] Version: Bumped `plugin.json` version if this is a release PR (and added dated section to CHANGELOG)
- [ ] Tests: Core headless tests pass locally
- [ ] Build: `make -j$(nproc)` (or Build task) succeeds
- [ ] No unintended source changes (limit diff noise)
- [ ] JSON backward compatibility preserved (if serialization touched)

## Testing Notes
Describe how you verified behavior (commands, patches, etc.).

## Additional Context
Optional extra information.
