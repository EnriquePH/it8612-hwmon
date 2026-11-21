# it8612 — Linux hwmon driver for the ITE IT8612E

A small, **read-only** Linux kernel module for the ITE **IT8612E** Super I/O
(chip ID `0x8612`). The in-kernel `it87` driver does not know this chip, so
`sensors-detect` reports it as an unknown ITE chip and there are no fan or board
temperature readings.

The driver exposes, through the standard hwmon interface (`sensors`,
`/sys/class/hwmon`, Prometheus node_exporter):

| Attribute | Register | Meaning |
| --- | --- | --- |
| `temp1_input` … `temp3_input` | `0x29` … `0x2b` | Temperatures, °C |
| `fan1_input` … `fan3_input` | `0x0d`–`0x0f` (low), `0x18`–`0x1a` (high) | Fan speeds, rpm (16-bit counters) |

Fans whose tachometer is disabled (register `0x13`, bits 4–6) are hidden.

It **never writes** to the environment controller: the fans stay under the
chip's own automatic control, as the BIOS set it up. The only writes are the
standard Super I/O configuration-space sequence (enter key, logical device
select, exit) used once at load time to find the controller.

## Tested hardware

| Machine | Board | CPU | Result |
| --- | --- | --- | --- |
| HP Pavilion 500-526ns | HP 2B35 (BIOS AMI 80.06) | AMD A8-6500 | temp1 CPU thermistor, temp2 board, temp3 = AMD Tctl; fan2 and fan3 |

On that board the register map was established by dumping the 256 registers
of the environment controller at rest, under full CPU load and while cooling
down (temp1 26 → 38 → 31 °C; temp3 following `k10temp`'s Tctl exactly; fan
counters giving about 935 and 850 rpm). The same map is the one `it87` uses for
the IT8603E, and loading `it87` with `force_id=0x8603` gives the same values.

Reports from other boards are welcome: open an issue with the output of
`sensors` and `sudo isadump -y 0x<base+5> 0x<base+6>`.

## Build and install

Requirements: the kernel headers and, for automatic rebuilds on kernel updates,
DKMS. On Debian/Ubuntu:

```bash
sudo apt install linux-headers-$(uname -r) dkms build-essential
```

Build and try it:

```bash
make
sudo rmmod it87 2>/dev/null   # it87 with force_id would hold the same ports
sudo insmod ./it8612.ko
sensors it8612-isa-*
```

Install with DKMS and load at boot:

```bash
sudo make dkms-install
echo it8612 | sudo tee /etc/modules-load.d/it8612.conf
```

Remove: `sudo make dkms-remove` and delete `/etc/modules-load.d/it8612.conf`.

Labels depend on how each board wires the sensors. An example for the HP 2B35
is in [`sensors.d/hp-2b35.conf`](sensors.d/hp-2b35.conf) (copy it to
`/etc/sensors.d/`).

## Module parameters

| Parameter | Default | Use |
| --- | --- | --- |
| `force_id` | 0 | Treat another ITE chip ID as an IT8612E (testing only) |
| `ignore_resource_conflict` | false | Load even if ACPI claims the controller's ports |

## Not supported (yet)

- PWM duty and fan control: read-only by design; the PWM scale is not confirmed.
- Voltages: the BIOS of the tested board sets no limits and their scaling is
  unknown.

## License

GPL-2.0-or-later. Super I/O access follows `drivers/hwmon/it87.c` from the Linux
kernel.
