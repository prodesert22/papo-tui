## Development

- Reorganize CI into isolated native and instrumented jobs, with individual scenario reports and reproducible test dependencies to diagnose failures and reduce validation time.
- Keep static analysis on an explicit Clang 18 toolchain and verify message-navigation state before accessing its actions.
