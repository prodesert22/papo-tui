## Fixes

- Improve video and animated GIF playback cadence, including Chafa/TTY and Light GIFs, while keeping keyboard controls responsive. Reduce Kitty graphics traffic with compressed pixels for smoother playback in larger windows.
- Enable hardware video decoding in packaged builds when compatible drivers are available (VA-API on Linux and D3D11VA on Windows), with automatic CPU fallback.

## Development

- Reorganize CI into isolated native and instrumented jobs, with individual scenario reports and reproducible test dependencies to diagnose failures and reduce validation time.
- Keep static analysis on an explicit Clang 18 toolchain and verify message-navigation state before accessing its actions.
