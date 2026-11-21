# Changelog

All notable changes to this project are listed here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and versions follow
[Semantic Versioning](https://semver.org/).

## [Unreleased]

### Changed

- Build with `make build` (plain `make` now lists the targets); the kbuild part moved to `Kbuild`.

## [0.1.0] - 2026-11-21

### Added

- Read-only hwmon driver for the ITE IT8612E Super I/O (chip ID `0x8612`): `temp1_input`–`temp3_input` and
  `fan1_input`–`fan3_input`, found through the Super I/O configuration space; disabled tachometers are hidden.
- DKMS support (`dkms.conf`, `make dkms-install`, `make dkms-remove`).
- Sensor labels for the HP 2B35 board (`sensors.d/hp-2b35.conf`).
