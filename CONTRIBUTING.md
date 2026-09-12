# Contributing

Thank you for considering contributing to this project!

## Quick Start

1. Fork the repository
2. Create a feature branch: `git checkout -b feature/my-feature`
3. Make your changes
4. Run the host tests: `pio test -e native -e native_max`
5. Build every firmware environment affected by the change; for shared engine
   changes, run all six `cli_esp32s2_*` / `cli_esp32s3_*` environments
6. Commit with a clear message: `git commit -m "feat: add X"`
7. Push and open a Pull Request

On Windows, invoke PlatformIO through `.\scripts\pio.cmd` with the same
arguments. Do not install a second PlatformIO Core when the wrapper cannot find
the VS Code-managed installation. CI additionally builds the native ESP-IDF
component on ESP-IDF 5.3 and 6.0 for both supported chips.

## Guidelines

### Code Style

- Follow the existing C++ style in the surrounding file
- Use `constexpr` instead of macros for constants
- Prefer explicit over implicit
- No heap allocations in steady-state library code

### Commits

- Use [Conventional Commits](https://www.conventionalcommits.org/) format:
  - `feat:` new feature
  - `fix:` bug fix
  - `docs:` documentation only
  - `refactor:` code change that neither fixes a bug nor adds a feature
  - `test:` adding or updating tests
  - `chore:` maintenance tasks

### Pull Requests

- Keep PRs focused (one feature/fix per PR)
- Update documentation if needed
- Add changelog entry under `[Unreleased]`
- Ensure CI passes

### What We Accept

- Bug fixes
- Documentation improvements
- Performance improvements (with benchmarks)
- New examples (if they demonstrate a common use case)

### What We Probably Won't Accept

- Breaking API changes without discussion
- Heavy dependencies
- Platform-specific code in the library core
- Features that add heap allocations in steady state

## Questions?

Open a GitHub Discussion or Issue for questions.
