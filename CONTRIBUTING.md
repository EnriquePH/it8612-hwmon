# Contributing

Thanks for helping. The most useful contribution is a **report from another board** with an IT8612E: it is the only
way to know whether the register map holds beyond the one tested board.

## Reporting a board

Open an issue with:

- the board or computer model and BIOS version (`sudo dmidecode -t baseboard -t bios`);
- the output of `sensors` and of `sudo dmesg | grep -i it8612`;
- if possible, a dump of the environment controller (read-only), taken at rest and again under load:

  ```bash
  sudo isadump -y 0x<base+5> 0x<base+6>    # base address from: sudo dmesg | grep -i "environment controller"
  ```

- what each fan header and temperature sensor is connected to, if you know it.

## Principles

- **Read-only.** The driver never writes to the environment controller (no PWM, no fan control, no alarms). Changes
  that write to it will not be accepted.
- **Verified registers only.** A register or a chip ID is added only after it has been checked on real hardware.
  Do not extend the driver to other ITE chips without a register dump that proves the same map.
- **Standard hwmon.** Values go through the hwmon API in its units (millidegrees Celsius, rpm). Board-specific labels
  belong in `sensors.d/`, not in the driver.

## Code

- Linux kernel coding style: `make lint` runs `checkpatch.pl --strict` (give its path with
  `CHECKPATCH=/path/to/linux/scripts/checkpatch.pl`) and must report no errors, warnings or checks.
- `make test` builds the module with `W=1` and fails on any warning.
- `make check` runs both; the GitHub Actions workflow runs them on every push.
- Commit messages follow [Conventional Commits](https://www.conventionalcommits.org/) (`feat:`, `fix:`, `docs:`…), and
  user-visible changes get a line in `CHANGELOG.md` under `[Unreleased]`.

By contributing you agree that your contribution is licensed under GPL-2.0-or-later, like the rest of the project.
