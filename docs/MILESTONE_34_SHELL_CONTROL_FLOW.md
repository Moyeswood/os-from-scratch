# Milestone 34: Shell Control Flow

## Overview

The Ring 3 shell parses each line into a bounded AST before executing it.
Operators, conditional branches, and loops reuse the existing command parser,
word expansion, builtins, pipelines, and job control.

## Supported syntax

- `cmd1 && cmd2`, `cmd1 || cmd2`, `! cmd`, and `(command; list)`.
- `if condition; then commands; elif condition; then commands; else commands; fi`.
- `while condition; do commands; done`.
- `for NAME in word1 word2; do commands; done`.
- `case WORD in PATTERN|PATTERN) commands ;; ... esac` with `|`-separated
  patterns, `*`/`?` glob matching, and a default `*` arm.
- `break` and `continue` target the innermost active loop.
- Both loop types accept one trailing stdin redirect: `done < file` or
  `done <file`. The filename supports the existing quoting, variable expansion,
  field splitting, and pathname expansion rules; it must resolve to one
  nonempty filename.

```sh
false && echo skipped
false || echo fallback
! false && echo success
(echo first; echo second)
if test -f /input.txt; then cat /input.txt; else echo missing; fi

for NAME in alpha beta; do echo "$NAME"; done

# Create a two-line input file.
echo first > /lines.txt
echo second >> /lines.txt
INPUT=/lines.txt
while read LINE; do echo "line=$LINE"; done < "$INPUT"
for LABEL in a b; do read LINE; echo "$LABEL=$LINE"; done < /lines.txt
```

## Architecture

1. `cf_scan()` records source spans without discarding quotes or redirections.
   Block terminators remain words and are recognized by parser stop masks.
2. Recursive descent parses lists, OR, AND, command-position keywords, and
   pipeline spans. The current grammar gives `&&` higher precedence than `||`;
   unlike POSIX shells, they are not equal-precedence left-associative operators.
   Parenthesized lists execute in the current shell, not a subshell.
3. `cf_node_pool` holds at most 128 nodes in static BSS. `cf_execute_line()`
   resets its cursor for each line; it does not allocate AST nodes on the user
   heap. Command substitutions fork, so COW isolates their pool rebuilds.
   The flat-image linker emits an `.image` byte after BSS, so pool size still
   affects the boot image size.
4. `cf_run_pipeline()` retokenizes the source span at execution time, preserving
   the existing variable, quoting, glob, redirection, and pipeline machinery.
5. `cf_execute()` dispatches through the shared input-redirection wrapper when
   needed, then `cf_execute_node()` runs the node. Negative sentinels propagate
   `break`, `continue`, and shell exit through nested AST nodes.

## Loop-level stdin ownership

`cf_parse_loop_input()` is shared by `while` and `for`. It records the filename
span rather than copying a truncated 48-byte path or expanding variables while
parsing. This makes preceding assignments on the same line visible when the
loop is entered.

`cf_execute_with_input()` applies the redirection once around the entire loop:

1. Expand the filename and require exactly one nonempty result.
2. Save the original fd 0 with `dup(0)` **before** replacing it.
3. Open the input, `dup2()` it onto fd 0, and close the temporary open descriptor.
4. Run all loop iterations against that shared open-file offset.
5. Restore the saved fd 0 and close its saved descriptor at one cleanup point,
   including normal completion, `break`, `continue`, and shell exit.

For nested redirects, the inner loop saves the outer file description. Restoring
it preserves the outer offset instead of reopening the file or restoring the
console prematurely. Without its own redirect, an inner loop reads the same
stdin as its outer loop.

Open/setup failures skip the loop body, report a diagnostic on stderr, and
return status 1. This also applies to an empty `for` list and a false `while`
condition: redirection happens before deciding whether to iterate.

## Validation

```sh
make -j4
make test-control
make test-shell
make test-all
```

`tools/test-control.py` drives QEMU's real keyboard path and checks serial
output relative to each command. New cases cover shared offsets, nested loops,
quoted filename variables, early exits, failed opens, empty loops, syntax
errors, and repeated redirections beyond the 16-descriptor limit. An isolated
run of the expanded suite against the pre-change image failed 11 of 41 checks,
confirming that the regression tests reach the missing behavior.

## case statement

`cf_parse_case()` reads one subject word, then arms of `PATTERN|PATTERN)`
bodies terminated by `;;`. Both the subject and patterns keep source spans and
are value-expanded at execution time (`cf_expand_value`: variable/quoting/
escape expansion only, no filename globbing), so a `*` in a pattern stays a
literal glob character for `glob_match()` and a subject keeps its full value.
`cf_parse_case_body()` parses each arm as a command list that stops at `;;` or
a bare `esac` (single `;` continues within an arm, matching POSIX). At most
8 arms and 4 patterns per arm; an unmatched subject returns status 0.

## Deliberate limits and follow-up

- Shell functions and positional parameters are not implemented; `case`
  patterns do not use filename expansion and there is no `|` in subjects.
- Compound commands do not implement general POSIX redirections: loops accept
  one trailing stdin redirect, not output/error redirects, here-documents, or
  arbitrary descriptor syntax.
- The current `for` implementation restores its environment snapshot on exit;
  assignments in its body therefore do not persist like POSIX shell assignments.
- The shell environment has 32 slots. Exhaustion and `for` variable errors need
  a separate policy; deleting user variables automatically would lose state.
- At most 8 `if` arms, 8 `case` arms, 16 source words per `for` list; loops have a
  1,000,000-iteration guard.
- Command-substitution exit-status propagation and full POSIX expansion remain
  incomplete. This shell is intentionally a small educational implementation.
