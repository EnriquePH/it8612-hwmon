# Changelog

All notable changes to this project are listed here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and versions follow
[Semantic Versioning](https://semver.org/).

## [Unreleased]

## [0.3.1] - 2026-11-27

### Changed

- README: install the kernel headers metapackage (`linux-headers-amd64`, `linux-headers-generic`), not
  `linux-headers-$(uname -r)`, so that DKMS can rebuild the module for the next kernel.
- `make dkms-install` warns if no kernel headers metapackage is installed.

## [0.3.0] - 2026-11-24

### Changed

- Fan counts are read again if the chip updates the counter between its two byte reads.
- `force_id` only applies to ITE chips (ID `0x86xx` or `0x87xx`) and logs a warning; a base address of
  `0xfff8` (unset) is rejected; ignoring an ACPI conflict logs a warning.
- `read` rejects attributes the driver does not provide; register masks and constants have names; the
  rpm conversion is a function of its own.
- CI: `actions/checkout@v5` (Node 24).

### Documentation

- README: what is checked on the HP 2B35 and what is inferred from the IT87 family; Linux 6.4 or later.

## [0.2.0] - 2026-11-22

### Added

- Continuous integration (GitHub Actions): build, `W=1` and `checkpatch.pl --strict` on Ubuntu, and a `W=1` build
  against the Debian 13 kernel headers, on every push.
- `CONTRIBUTING.md` (how to report a board) and a Troubleshooting section in the README.

### Changed

- `make dkms-install` removes any other installed version of the module first, so upgrading is one command;
  the README explains how to upgrade.

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

[Unreleased]: https://github.com/EnriquePH/it8612-hwmon/compare/v0.3.1...HEAD
[0.3.1]: https://github.com/EnriquePH/it8612-hwmon/compare/v0.3.0...v0.3.1
[0.3.0]: https://github.com/EnriquePH/it8612-hwmon/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/EnriquePH/it8612-hwmon/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/EnriquePH/it8612-hwmon/releases/tag/v0.1.0
