#include "nvtop/device_card.h"
#include "nvtop/interface.h"
#include "nvtop/interface_internal_common.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void check_snapshot_links(struct gpu_info *device, bool populated) {
  LIST_HEAD(devices);
  list_add_tail(&device->list, &devices);
  FILE *output = tmpfile();
  assert(output);
  int saved_stdout = dup(STDOUT_FILENO);
  assert(saved_stdout >= 0);
  fflush(stdout);
  assert(dup2(fileno(output), STDOUT_FILENO) >= 0);
  print_snapshot(&devices, false, true);
  assert(dup2(saved_stdout, STDOUT_FILENO) >= 0);
  close(saved_stdout);
  rewind(output);
  char json[8192];
  size_t size = fread(json, 1, sizeof(json) - 1, output);
  json[size] = '\0';
  fclose(output);
  if (populated) {
    assert(strstr(json, "\"pcie_gen\": 4,"));
    assert(strstr(json, "\"pcie_width\": 16,"));
    assert(strstr(json, "\"pcie_rx_mb_per_ms\": 123,"));
    assert(strstr(json, "\"pcie_tx_mb_per_ms\": 0,"));
    assert(strstr(json, "\"ecc_corrected\": 18446744073709551615,"));
    assert(strstr(json, "\"ecc_uncorrected\": 4294967296,"));
    assert(strstr(json, "\"hccs_links\": 7,"));
    assert(strstr(json, "\"hccs_healthy\": false,"));
    assert(strstr(json, "\"hccs_rx_bytes_per_second\": 1048576.000,"));
    assert(strstr(json, "\"hccs_tx_bytes_per_second\": 0.000,"));
  } else {
    const char *keys[] = {"pcie_gen", "pcie_width", "pcie_rx_mb_per_ms", "pcie_tx_mb_per_ms", "ecc_corrected",
                          "ecc_uncorrected", "hccs_links", "hccs_healthy", "hccs_rx_bytes_per_second",
                          "hccs_tx_bytes_per_second"};
    for (unsigned i = 0; i < ARRAY_SIZE(keys); ++i) {
      char expected[80];
      snprintf(expected, sizeof(expected), "\"%s\": null,", keys[i]);
      assert(strstr(json, expected));
    }
  }
  assert(strstr(json, "\"npu_clock\": null,"));
  assert(strstr(json, "\"mem_total\": null,"));
  list_del(&device->list);
}

static void check_render(struct gpu_info *device, unsigned cols, unsigned rows, bool clocks, bool populated) {
  FILE *output = tmpfile(), *input = tmpfile();
  assert(output && input);
  SCREEN *screen = newterm("xterm-256color", output, input);
  assert(screen);
  assert(resizeterm(rows, cols) == OK);
  unsigned char colors[MAX_LINES_PER_PLOT] = {0};
  apply_plot_colors(colors);
  nvtop_interface_gpu_opts gpu_options[8] = {0};
  nvtop_interface_option options = {.has_gpu_info_bar = clocks, .gpu_specific_opts = gpu_options};
  struct device_cards *cards = device_cards_create(8);
  LIST_HEAD(devices);
  struct gpu_info fixtures[8];
  for (unsigned i = 0; i < 8; ++i) {
    fixtures[i] = *device;
    fixtures[i].device_id = i;
    list_add_tail(&fixtures[i].list, &devices);
  }
  struct device_card_layout layout = device_cards_layout(cards, &devices, &options, rows, cols, false);
  assert(!layout.too_small && layout.rows == 3);
  struct window_position positions[8], plots[MAX_CHARTS], process, setup;
  unsigned plot_count, map[8];
  compute_sizes_from_layout(8, layout.rows, layout.cols, rows - 1, cols, gpu_options,
                            process_default_displayed_field(), positions, &plot_count, plots, map,
                            &process, &setup, false);
  device_cards_allocate(cards, positions);
  device_cards_draw(cards, &devices, &options);
  doupdate();
  for (unsigned i = 0; i < 8; ++i) {
    unsigned x = positions[i].posX, y = positions[i].posY, card_cols = positions[i].sizeX;
    char first[512], second[512], bars[512];
    mvwinnstr(curscr, y, x, first, card_cols);
    mvwinnstr(curscr, y + 1, x, second, card_cols);
    mvwinnstr(curscr, y + 2, x, bars, card_cols);
    assert(strstr(first, "NPU"));
    assert(strstr(second, "TEMP") && strstr(second, "POW"));
    assert(strstr(bars, "NPU[") && strstr(bars, "HBM["));
    if (layout.cols >= 76) {
      assert(strstr(first, populated ? "PCIe GEN4@16x" : "PCIe N/A"));
      assert(strstr(first, populated ? "RX 123MB/ms" : "RX N/A"));
      assert(strstr(first, populated ? "TX 0MB/ms" : "TX N/A"));
      assert(strstr(second, populated
          ? (GPUINFO_DYNAMIC_FIELD_VALID(&device->dynamic_info, ecc_corrected) ? "ECC 1844..." : "ECC N/A/...")
          : "ECC N/A/N/A"));
      assert(strstr(second, populated
          ? (device->dynamic_info.hccs_healthy ? "HCCS 7x OK" : "HCCS 7x WARN") : "HCCS N/A N/A"));
      const char *rx = "RX N/A";
      if (GPUINFO_DYNAMIC_FIELD_VALID(&device->dynamic_info, hccs_rx_bytes_per_second))
        rx = device->dynamic_info.hccs_rx_bytes_per_second >= 1073741824. ? "RX 1.000GiB/s" : "RX 1.000MiB/s";
      assert(strstr(second, rx));
      assert(strstr(second, populated ? "TX 0.000KiB/s" : "TX N/A"));
      if (populated && !device->dynamic_info.hccs_healthy)
        assert(PAIR_NUMBER(mvwinch(curscr, y + 1, x + 35)) == warning_color);
      if (clocks) {
        assert(strstr(first, GPUINFO_DYNAMIC_FIELD_VALID(&device->dynamic_info, gpu_clock_speed)
            ? "NPU 1234MHz" : "NPU N/A"));
        assert(strstr(first, GPUINFO_DYNAMIC_FIELD_VALID(&device->dynamic_info, mem_clock_speed)
            ? "HBM 1600MHz" : "HBM N/A"));
      }
      if (GPUINFO_DYNAMIC_FIELD_VALID(&device->dynamic_info, fan_rpm))
        assert(strstr(first, "FAN 4321RPM"));
    } else {
      assert(!strstr(first, "PCIe") && !strstr(second, "HCCS"));
    }
    for (unsigned col = 0; col < card_cols; ++col)
      assert((mvwinch(curscr, y + 3, x + col) & A_CHARTEXT) == ' ');
    if (i && positions[i - 1].posY == y) {
      for (unsigned row = y; row < y + 3; ++row) {
        assert((mvwinch(curscr, row, x - 1) & A_CHARTEXT) == ' ');
        assert((mvwinch(curscr, row, x - 2) & A_CHARTEXT) == ' ');
      }
    }
  }
  device_cards_destroy(cards);
  endwin();
  delscreen(screen);
  fclose(input);
  fclose(output);
}

int main(void) {
  struct gpu_info device = {0};
  struct gpuinfo_dynamic_info *info = &device.dynamic_info;
  check_snapshot_links(&device, false);
  check_render(&device, 80, 24, false, false);
  check_render(&device, 160, 50, false, false);
  check_render(&device, 320, 50, true, false);

  SET_GPUINFO_DYNAMIC(info, pcie_link_gen, 4);
  SET_GPUINFO_DYNAMIC(info, pcie_link_width, 16);
  SET_GPUINFO_DYNAMIC(info, pcie_rx_mb_per_ms, 123);
  SET_GPUINFO_DYNAMIC(info, pcie_tx_mb_per_ms, 0);
  SET_GPUINFO_DYNAMIC(info, ecc_corrected, ULLONG_MAX);
  SET_GPUINFO_DYNAMIC(info, ecc_uncorrected, 4294967296ULL);
  SET_GPUINFO_DYNAMIC(info, hccs_links, 7);
  SET_GPUINFO_DYNAMIC(info, hccs_healthy, false);
  SET_GPUINFO_DYNAMIC(info, hccs_rx_bytes_per_second, 1048576.);
  SET_GPUINFO_DYNAMIC(info, hccs_tx_bytes_per_second, 0.);
  check_snapshot_links(&device, true);
  check_render(&device, 160, 50, false, true);
  check_render(&device, 312, 50, false, true);
  check_render(&device, 320, 50, false, true);
  check_render(&device, 320, 50, true, true);
  SET_GPUINFO_DYNAMIC(info, gpu_clock_speed, 1234);
  SET_GPUINFO_DYNAMIC(info, mem_clock_speed, 1600);
  SET_GPUINFO_DYNAMIC(info, fan_rpm, 4321);
  check_render(&device, 320, 50, true, true);
  check_render(&device, 312, 50, false, true);
  SET_GPUINFO_DYNAMIC(info, hccs_healthy, true);
  SET_GPUINFO_DYNAMIC(info, hccs_rx_bytes_per_second, 1073741824.);
  check_render(&device, 312, 50, false, true);
  RESET_GPUINFO_DYNAMIC(info, ecc_corrected);
  RESET_GPUINFO_DYNAMIC(info, hccs_rx_bytes_per_second);
  check_render(&device, 312, 50, false, true);
  puts("UI links: missing values, native PCIe units, ECC width, HCCS scales and layout fallback passed");
  return 0;
}
