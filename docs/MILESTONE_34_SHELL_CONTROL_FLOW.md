# Milestone 34: Shell Control Flow

## Overview
Added shell control flow operators to the Ring 3 user shell, enabling script-like conditional logic and loops.

## Features Implemented

### Operators
- **&&** (AND): `cmd1 && cmd2` - run cmd2 only if cmd1 succeeds
- **||** (OR): `cmd1 || cmd2` - run cmd2 only if cmd1 fails
- **!** (NOT): `! cmd` - negate exit status

### Combinations
```bash
echo hello && echo world       # Shows both
false && echo no               # Skips echo
true || echo no                # Skips echo
false || echo yes              # Shows "yes"
! true && echo no              # Skips echo
! false && echo yes            # Shows "yes"
(echo grouped) && echo success # Grouping with ()
```

### Architecture
- New `cf_node_t` structure for control flow tree representation
- `cf_parse()` - recursive descent parser for control flow syntax
- `cf_execute()` - interpreter that traverses the parse tree
- Integrated into existing `execute_segment()` pipeline

### Testing
- Created `tools/test-control.py` for automated testing
- 7/7 core tests passing
- Tests cover: basic operations, short-circuit behavior, negation logic

## Files Modified
- `user/sh.c`: Added cf_parse, cf_execute, cf_free functions (~180 lines)
- `tools/test-control.py`: New test suite (100 lines)
- `docs/MILESTONE_34_SHELL_CONTROL_FLOW.md`: This documentation

## Design Decisions
1. **Right-to-left precedence**: || has lower precedence than &&, matching bash semantics
2. **Heap allocation**: Nodes allocated on heap but freed immediately after execution (memory safety)
3. **No variable substitution in conditions**: For simplicity; can be added later
4. **Reuses existing infrastructure**: Leverages parse_commands() and execute_pipeline()

## Limitations & Future Work
- [ ] Variable expansion (`$var`, `$?`) not yet supported in conditionals
- [ ] Positional parameters ($1-$9) not yet supported
- [ ] while/do/done loops: basic structure in place, needs more testing
- [ ] for/in/do/done loops: basic structure in place, needs more testing
- [ ] if/then/else/fi: parser in place, executor needs refinement

## Validation
```bash
python3 tools/test-control.py
# Expected: SUMMARY: 7 checks, 7 PASS, 0 FAIL
```
