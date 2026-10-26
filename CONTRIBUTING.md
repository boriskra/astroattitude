# Contributing

Bug reports, documentation improvements and tested fixes are welcome. Start with the
[build guide](docs/BUILD.md), then open a focused pull request against `main`.
Use [GitHub issues](https://github.com/boriskra/astroattitude/issues) to discuss changes.

## A good change

Explain the problem, resulting behavior and how you tested it. Add independent
synthetic controls for numerical changes, including failure cases. Keep public API
contracts and all bindings consistent. Do not infer accuracy from fit residuals.
Run `python3 tools/check_publication.py`, the relevant tests, and `git diff --check`.

## Keep observations private

Do not attach real sky images, exact observation times, GPS, star-coordinate lists,
attitudes, sensor logs, device calibration or capture-derived reports. These can
identify an observation site even when EXIF metadata has been removed. Reproduce
issues with a small independently generated synthetic case. Strip personal paths
from build logs. Report credentials or vulnerabilities through [SECURITY.md](SECURITY.md).

## Licensing

Contributions to original code are under the [MIT license](LICENSE). Keep third-party
copyrights and license notices intact. Do not add catalog or time-data assets without
an explicit redistribution review. No contributor license agreement is required.
