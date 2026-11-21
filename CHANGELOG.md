# Changelog

All notable changes to this project are listed here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and versions follow
[Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

- Continuous integration (GitHub Actions): build, `W=1` and `checkpatch.pl --strict` on Ubuntu, and a `W=1` build
  against the Debian 13 kernel headers, on every push.
- `CONTRIBUTING.md` (how to report a board) and a Troubleshooting section in the README.

### Fixed

- A fan count of `0` is now reported as no data (`N/A`), as in `it87`, instead of 0 rpm; `0xffff` still reads 0 rpm.

## [0.1.0] - 2026-11-21

### Added

- Read-only hwmon driver for the ITE IT8612E Super I/O (chip ID `0x8612`): `temp1_input`–`temp3_input` and
  `fan1_input`–`fan3_input`, found through the Super I/O configuration space; disabled tachometers are hidden.
- DKMS support (`dkms.conf`, `make dkms-install`, `make dkms-remove`).
- Sensor labels for the HP 2B35 board (`sensors.d/hp-2b35.conf`).
- Build with `make build` (`make` lists the targets: `lint` runs `checkpatch.pl`, `test` builds with `W=1`); the
  kernel build is described in `Kbuild`.

[Unreleased]: https://github.com/EnriquePH/it8612-hwmon/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/EnriquePH/it8612-hwmon/releases/tag/v0.1.0
