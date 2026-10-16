# NVTOP capability reference for astop

Research date: 2026-10-16. This is a feature inventory and visual reference, not an astop implementation plan.

## Evidence and version boundary

The implementation inspected is upstream `master` at [`71094a9f7dfbfe7cbe9faebfdf055e580a034283`](https://github.com/Syllo/nvtop/commit/71094a9f7dfbfe7cbe9faebfdf055e580a034283), dated 2026-10-04. The [3.3.2 release](https://github.com/Syllo/nvtop/releases/tag/3.3.2) was published on 2026-02-08. Master's [CMake version string is also 3.3.2](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/CMakeLists.txt#L7), but that does not establish release parity. Unless stated otherwise, the capabilities below describe the pinned source, not a distribution package or release binary.

NVTOP already has a Huawei Ascend backend. The pinned README says DCMI support was tested on **910B3**, uses the logical-device API when available, and falls back to the legacy card/device API. Older search snippets describing much narrower Ascend support should not override this newer, directly inspected implementation. Actual metric availability still depends on hardware and installed driver. [README](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/README.markdown#L161-L170)

## What NVTOP can do

| Area | Capability | Evidence |
|---|---|---|
| Live terminal monitor | An ncurses, htop-style view of multiple GPUs and accelerators, with per-device summaries, scrolling time plots, a shared process table, and keyboard controls. | [README and screenshot](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/README.markdown#L4-L20) |
| Device summaries | Device name and PCIe information; accelerator and memory clocks; temperature; fan percentage or RPM; power draw/limit; utilization and used/total memory bars. Fields depend on the backend. | [Header rendering](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/src/interface.c#L941-L1250) |
| Optional device detail | Encoder/decoder meters appear when active and hide after an idle timer. Supported devices can show corrected/uncorrected ECC counts, NVIDIA NVLink link/throughput information, extra clock domains, and static specs. `-i` can add NVLink FLIT/ECC/CRC counters; this is integrated header detail, not a topology screen. | [Meters and optional information](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/src/interface.c#L884-L1290) |
| Time history | Up to **four selected metrics per device**, selected individually or across all devices; configurable line colors and time direction. Default plots are device utilization and memory utilization. History uses an in-memory ring buffer. | [Chart setup](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/src/interface_setup_win.c#L338-L500), [defaults](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/include/nvtop/interface_options.h#L62-L91), [history sampling](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/src/interface.c#L2091-L2187) |
| Process attribution | Configurable columns: PID, USER, DEV, TYPE (graphics/compute/both), device utilization, encoder utilization, decoder utilization, device memory bytes/percentage, CPU usage, host resident memory, and command line. Unsupported columns show N/A. | [Columns and rendering](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/src/interface.c#L1584-L1810) |
| Process controls | Sort by a displayed column, ascending/descending; select and scroll rows; send a selected Unix signal to a process. The latter is a real process-control action subject to OS permissions. | [Controls](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/src/interface.c#L2337-L2485) |
| Display selection | Hide the process list, hide NVTOP's own PID, and select which devices to monitor. No arbitrary command/user text-search filter was found in the inspected controls. | [Process setup](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/src/interface_setup_win.c#L545-L655), [device selection](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/src/interface_setup_win.c#L979-L988) |
| Preferences | Configure update interval, colors, temperature units, charts, columns, sorting, and device selection; explicitly save and reload preferences. Default refresh interval is one second. | [Options defaults](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/src/interface_options.c#L118-L135), [manual](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/manpage/nvtop.in#L71-L156) |
| Script output | `--snapshot` emits a JSON array and exits; `--loop` emits successive snapshots. Unavailable values are `null`; many available numeric values are strings, sometimes with units. Loop output is successive arrays rather than one accumulated JSON document. | [Snapshot loop](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/src/nvtop.c#L263-L292), [serializer](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/src/interface.c#L2616-L2887) |

## How it visualizes information

The official [main screenshot](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/screenshot/NVTOP_ex1.png) and [setup screenshot](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/screenshot/Nvtop-config.png) show the visual pattern. These are upstream examples, not evidence that Ascend supplies every visible field.

Illustrative schematic only; invented values and simplified spacing:

```text
Device 0  accelerator name         PCIe link / receive / transmit
Core 1200MHz   HBM 1600MHz   TEMP 55°C   FAN N/A   POW 180W / 350W
NPU [||||||||||||       62%]  MEM [|||||      16GiB / 64GiB]

100 ┤                 ╭──╮                    device utilization
 50 ┤   ╭─────╮  ╭────╯  ╰───                memory utilization
  0 ┼───╯     ╰──╯───────────────────
      older samples  →  newer samples

PID    USER  DEV TYPE     NPU   NPU MEM      CPU  HOST MEM  Command
12345  user    0 Compute  N/A   12000MiB     25%  500MiB   python train.py
F2 Setup    F6 Sort    F9 Kill    F10 Quit    F12 Save
```

The **header memory bar** uses used bytes / total bytes. The **memory chart** uses the backend's `mem_util_rate`, which is not guaranteed to mean the same thing across vendors; the inspected Ascend backend derives HBM occupancy from bytes, or uses DDR's reported utilization. Do not label all vendors' memory charts as memory bandwidth. [Header math](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/src/interface.c#L1042-L1067), [Ascend memory sampling](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/src/extract_gpuinfo_ascend.c#L459-L568)

Available chart selectors and their actual transformations:

| Plot | Plotted value |
|---|---|
| Device, memory, encoder, decoder, effective load | Corresponding backend percentage. Effective load is backend-specific. |
| Temperature | Celsius, capped at 100 for plotting; this is not a percentage and the plot stays Celsius even if header units change. |
| Power | Current draw / reported maximum × 100, capped at 100; requires both values. |
| Core and memory clock | Current / maximum × 100; requires both frequencies. |
| Fan | Fan speed percentage; an RPM-only backend does not populate this chart. |
| PCIe RX / TX | Throughput as a percentage of maximum PCIe link bandwidth, requiring valid link specifications. |
| HVX / HMX | Backend-specific utilization; these selectors exist for Qualcomm NPU support and are not Ascend AI Core/AI CPU metrics. |

These transformations are defined in [history sampling](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/src/interface.c#L2091-L2187) and [plot labels](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/src/interface.c#L2214-L2259). **Missing data is not consistently visualized as a gap:** that sampling code initializes missing values to zero before pushing them into history. Header N/A and snapshot null are more explicit than a zero-valued graph. Active encoder/decoder meters can also temporarily display zero when their current sample is unavailable. This is an existing behavior worth improving if astop adopts this visual pattern. [Meter behavior](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/src/interface.c#L1008-L1025)

## Keyboard, configuration, and CLI reference

| Control | Action |
|---|---|
| `F2` | Open setup: General, Devices, Chart, Processes, GPU Select. |
| `F12` | Save preferences. |
| `F6`, then arrows and Enter | Choose sort column. `+` / `-` set ascending / descending order. |
| Up/Down or `k`/`j` | Select process or menu entry. |
| Left/Right or `h`/`l` | Scroll process row horizontally; navigate setup. |
| `F9`, then signal and Enter | Send signal to selected PID. |
| `F5` or Ctrl-L | Rebuild/redraw terminal windows. |
| `q`, `F10`, Esc | `q` quits; `F10` quits from the main view; Esc first dismisses an open setup/action panel, then quits from the main view. |

Sources: [key handling](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/src/interface.c#L2337-L2485), [quit handling](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/src/nvtop.c#L399-L456), [setup categories](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/src/interface_setup_win.c#L31-L32).

Preferences use `$XDG_CONFIG_HOME/nvtop/interface.ini`, defaulting to `~/.config/nvtop/interface.ini`; `-c` overrides the location. The manual directs users to edit through setup and save with F12. [Configuration manual](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/manpage/nvtop.in#L146-L156)

Useful invocations from the [CLI implementation](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/src/nvtop.c#L53-L117):

```sh
nvtop                 # interactive
nvtop -d 5            # update every 0.5 seconds; -d is tenths of a second
nvtop -P              # hide process list
nvtop -C              # monochrome
nvtop -f              # Fahrenheit in device temperature display
nvtop -u              # automatically scale process memory units
nvtop -r              # reverse time direction
nvtop -E -1           # keep encoder/decoder meters visible
nvtop -i              # optional dynamic detail bar
nvtop -X              # static specs and secondary clock domains
nvtop -s              # one noninteractive snapshot
nvtop -l -d 10        # successive noninteractive snapshots, one second apart
nvtop -c /path/interface.ini
nvtop --help
```

`-p` / `--no-plot` disables history charts in the inspected source by clearing every device's selected chart metrics; the live device headers and their bars remain. The manual's description of retaining an aggregate plot conflicts with this implementation, so this inventory follows the source. [Implementation](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/src/nvtop.c#L323-L327), [conflicting manual text](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/manpage/nvtop.in#L31-L34)

## What the inspected Ascend backend actually supplies

All rows are **conditional on successful driver queries**, not promises for every Ascend model. The backend clears the dynamic validity state every refresh; unsuccessful queries remain unavailable. [Refresh implementation](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/src/extract_gpuinfo_ascend.c#L997-L1117)

| Field | Ascend mapping and important limit | Evidence |
|---|---|---|
| Device identity | Logical-device enumeration preferred, legacy card/device enumeration fallback; explicitly identified non-NPU units excluded. Chip/NPU name, AI Core count, and HBM/DDR type when available. | [Enumeration and static fields](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/src/extract_gpuinfo_ascend.c#L794-L995) |
| Main NPU utilization | One aggregate value: NPU utilization first, AI Core fallback, then multi-utilization query. Separate AI CPU, vector/core breakdowns are not published by this backend. | [Selection logic](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/src/extract_gpuinfo_ascend.c#L1063-L1079) |
| Memory | HBM preferred, then DDR APIs; total, used, free, utilization, and frequency. Older DDR APIs reconstruct byte usage from rounded utilization; newer available-byte API is preferred. It chooses one source, not separate simultaneous HBM/DDR panels. | [Memory fallbacks](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/src/extract_gpuinfo_ascend.c#L459-L568), [publication](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/src/extract_gpuinfo_ascend.c#L1033-L1061) |
| Clocks | Current and maximum AI Core frequencies; selected memory source's current and maximum frequency when queryable. | [Clock queries](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/src/extract_gpuinfo_ascend.c#L1004-L1061) |
| Temperature and power | Device temperature and power; rated power is accepted only in the backend's 150–600 W range and cached for five seconds by default. Threshold sensors can supply temperature slowdown/shutdown limits. No power-control action is exposed. | [Power validation](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/src/extract_gpuinfo_ascend.c#L719-L733), [refresh](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/src/extract_gpuinfo_ascend.c#L1081-L1105), [thresholds](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/src/extract_gpuinfo_ascend.c#L984-L994) |
| ECC | Sum of HBM and DDR single-bit and double-bit error counters into generic corrected/uncorrected totals; no per-memory error breakdown in the UI. | [ECC collection](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/src/extract_gpuinfo_ascend.c#L735-L760) |
| DVPP | Device-wide VENC and VDEC percentages only; requires legacy IDs and the optional DVPP API. | [DVPP](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/src/extract_gpuinfo_ascend.c#L699-L716) |
| Fan | Driver's average fan RPM, if fans and legacy APIs exist. It does not publish fan percentage, so the generic fan percentage chart has no valid Ascend input. | [Fan](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/src/extract_gpuinfo_ascend.c#L683-L697), [chart input](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/src/interface.c#L2135-L2139) |
| PCIe | BDF via DCMI; current/maximum link generation and width via Linux sysfs. Optional bandwidth profiling uses 100 ms samples, refreshed every five seconds by default and cached between samples; driver call may block. | [PCIe collection](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/src/extract_gpuinfo_ascend.c#L608-L680), [sampling constants](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/src/extract_gpuinfo_ascend.c#L39-L51) |
| Processes | DCMI supplies PID and device memory bytes; all entries are marked compute. Query buffer is 64 entries; failed/invalid queries publish an empty snapshot rather than preserving stale entries. No per-process NPU/ENC/DEC percentage is collected here. | [Process collector](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/src/extract_gpuinfo_ascend.c#L1119-L1157) |
| Host process enrichment | Shared code adds user, command, CPU time/rate, resident/virtual memory, and percentage of device memory when OS access and totals permit. These are host observations, not extra DCMI telemetry. | [Shared enrichment](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/src/extract_gpuinfo.c#L214-L279) |

There is no Ascend HCCS/interconnect topology or traffic panel, device health/error-code view, per-core utilization grid, model/task attribution, or profiling timeline in this inspected Ascend collector. These require separate requirements and evidence, rather than relabeling generic GPU fields. The concrete positive inventory above is the appropriate compatibility baseline. [Complete Ascend collector](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/src/extract_gpuinfo_ascend.c)

## Platform and backend coverage

The pinned build declares NVIDIA/NVML, AMD amdgpu and legacy radeon, Intel i915/Xe, Qualcomm Adreno/MSM, Apple/Metal, Mali/Panfrost and Panthor, Huawei Ascend/DCMI, Broadcom VideoCore/v3d, Google TPU, Rockchip NPU, MetaX, Enflame, Tenstorrent, and Iluvatar CoreX backends. Most target Linux drivers/interfaces; Apple support is a separate macOS/Metal build and the README calls it limited/initial. Availability is build-time and driver dependent; a backend existing in master does not establish every packaged binary includes it. [Build options and platform conditions](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/CMakeLists.txt#L61-L153), [backend notes](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/README.markdown#L87-L213)

Ascend is opt-in with `-DASCEND_SUPPORT=ON`; custom installations can use `-DASCEND_ROOT=/path/to/Ascend`. Some other backends need suitable kernel versions or permissions for process and memory data; those requirements should not be generalized to Ascend. [Ascend build instructions](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/README.markdown#L161-L170)

## Sources

- [Pinned upstream repository](https://github.com/Syllo/nvtop/tree/71094a9f7dfbfe7cbe9faebfdf055e580a034283), including README and CMake build options.
- [Release 3.3.2](https://github.com/Syllo/nvtop/releases/tag/3.3.2), used only for the release boundary, not as proof of master feature parity.
- [Manual](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/manpage/nvtop.in) and [CLI](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/src/nvtop.c).
- [Interface, charts, controls, and snapshots](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/src/interface.c), [setup](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/src/interface_setup_win.c), and [options](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/src/interface_options.c).
- [Ascend collector](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/src/extract_gpuinfo_ascend.c) and [shared process enrichment](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/src/extract_gpuinfo.c).
- [Official main screenshot](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/screenshot/NVTOP_ex1.png) and [official setup screenshot](https://github.com/Syllo/nvtop/blob/71094a9f7dfbfe7cbe9faebfdf055e580a034283/screenshot/Nvtop-config.png).

Verification: source inspection and upstream visual review only; no NVTOP build or live Ascend hardware run was performed. Driver-dependent telemetry and tagged-release parity remain unverified.
