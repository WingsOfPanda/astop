# astop — Ascend Top

An Ascend-only terminal monitor derived from [nvtop](https://github.com/Syllo/nvtop).
Validated on Ubuntu 24.04 on ARM64 with eight Ascend 910B4-1 devices, 64 GiB
HBM per device, and the Ascend 26.1.1 driver. Other models and driver versions
have not been validated.

Run `astop` for live device summaries, NPU and memory history charts, and a
sortable process table. All devices are selected initially. The interface adapts
to the terminal size; a wider terminal gives charts and process details more room.
The application observes running workloads and has no process-kill control.

## Build and install

The build requires a C compiler, Make, pkg-config, ncurses development files,
and an installed Ascend driver with its DCMI library. It does not require CMake
or a running nvtop binary. On Ubuntu, install the build tools with:

```sh
sudo apt install build-essential pkg-config libncurses-dev
```

Then clone, build, and install:

```sh
git clone https://github.com/WingsOfPanda/astop.git
cd astop
make
make install PREFIX="$HOME/.local"
"$HOME/.local/bin/astop"
```

The build produces `build/astop`; installation places it in `~/.local/bin`.
Add that directory to your shell's PATH to invoke it as `astop`.
The default DCMI library directory is `/usr/local/Ascend/driver/lib64/driver`.
For a different driver installation, pass `DCMI_LIBDIR=/path/to/library/directory`
to `make`.

Run `make test` for the focused regression checks. `make smoke` additionally
requires Python 3, tmux, and the eight-device 910B4-1 reference configuration.
It reads live telemetry and exercises isolated terminal sessions with temporary
settings; it does not start workloads or signal existing workload processes.

## Use

```sh
astop                  # interactive monitor
astop -d 10            # refresh every second; units are tenths of a second
astop -s               # one JSON snapshot
astop -l -d 10         # successive JSON snapshots every second
astop -P               # hide the process table
astop -p               # hide history charts
astop -i               # extra clocks, PCIe, and other available device details
astop -X               # also show available static device specifications
astop -C               # monochrome
astop --help
```

| Key | Action |
| --- | --- |
| F2 | Configure charts, process columns, and selected devices |
| F6 | Choose the process sort column |
| + / - | Ascending / descending sort |
| Arrow keys or hjkl | Navigate and scroll |
| F12 | Save settings |
| Ctrl-L | Redraw |
| q | Quit |

Settings are separate from nvtop, under
`${XDG_CONFIG_HOME:-$HOME/.config}/astop/interface.ini`.

Metrics depend on successful DCMI queries. Unavailable readings show N/A or JSON
null, and missing history samples leave gaps. In particular, the inherited Ascend
collector supplies process IDs and device-memory usage, but not per-process NPU utilization. Host
CPU and memory readings come from Linux process information.

Loop mode emits successive JSON arrays, not one enclosing JSON document. Use a
streaming JSON decoder. Each device has a numeric `device_id` and a `pci_bus_id`;
many measurements retain nvtop's string values and units, such as `"50%"` and
`"1800MHz"`. Missing metrics are `null`, and a successful query with no devices
returns `[]`. Device IDs remain consistent when selecting a subset in the UI.

## Origin and appearance

The implementation starts from nvtop commit
[`71094a9f7dfbfe7cbe9faebfdf055e580a034283`](https://github.com/Syllo/nvtop/commit/71094a9f7dfbfe7cbe9faebfdf055e580a034283),
retaining its applicable copyright notices and GPLv3-or-later licensing.
Other accelerator backends and platform-specific implementations are excluded.
See [the nvtop reference](docs/nvtop-reference.md) for the source inventory.

The dark terminal theme uses Ascend's documented auxiliary blue-green (`#30B5C5`)
for selection and NPU charts, soft blue (`#7AA2F7`) for memory charts, and amber
for temperature warnings. Black text on the teal selection background keeps the
selected row readable. Soft blue is an application choice for chart contrast.
This is a terminal adaptation of the auxiliary palette, rather than the red logo
palette. The source is the official
[Ascend visual guide, main and auxiliary colors](https://www.hiascend.com/s/ascendstatic/lst/files/pdf/Visual_Specification_for_Brand_Authorization_V1.0.pdf).
This is an independent project, not an official Huawei product.
