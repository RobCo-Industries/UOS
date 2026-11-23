# Contributing to UOS

## Before you open a pull request

- `make CI=1 all test-host` passes.
- `make test-target BOARD=<board>` passes on the boards you touched. CI runs all four.
- New pure code has host tests that can fail. New behaviour has a target test or a reason why not.
- Docs match the code. Nothing is validated on real hardware: do not claim it.

## Code

- C gnu11, 4-space indent, snake_case.
- Prefix `uos_` in the kernel and services. Modules use the short prefix of the design.
- Use the library's `k_` functions, not libc.
- Pure files have no hardware access, no kernel calls and no allocation.
- Every limit is a named constant. Every error is a named code.

## Prose

Short plain sentences in the active voice. No em dashes. No filler or hype. Sentence-case headings. Lore text is original and marked as lore.

## Commits

Plain imperative subjects. One topic per commit. Each commit builds.
