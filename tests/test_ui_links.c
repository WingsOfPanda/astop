#include "../src/interface.c"

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
  start_color();
  unsigned char colors[MAX_LINES_PER_PLOT] = {0};
  initialize_colors(colors);
  struct device_window window;
  struct nvtop_interface interface = {.devices_win = &window, .options.has_gpu_info_bar = clocks};
  struct extra_info_item items[MAX_EXTRA_INFO_ITEMS];
  unsigned primary_count = collect_primary_info(device, &interface.options, items, MAX_EXTRA_INFO_ITEMS);
  assert(collect_extra_info(device, &interface.options, items, MAX_EXTRA_INFO_ITEMS) == 0);
  unsigned primary_cols = 64 + primary_count * PRIMARY_INFO_COLS +
      (GPUINFO_DYNAMIC_FIELD_VALID(&device->dynamic_info, fan_rpm) ? 12 : 0);
  interface.device_header_cols = device_length(8, rows, cols, 0, primary_cols, clocks);
  unsigned per_row = device_cards_per_row(8, interface.device_header_cols, cols);
  unsigned card_cols = (cols - 2 * (per_row - 1)) / per_row;
  assert(card_cols >= interface.device_header_cols);
  unsigned margin = (cols - per_row * card_cols - 2 * (per_row - 1)) / 2;
  LIST_HEAD(devices);
  list_add_tail(&device->list, &devices);
  for (unsigned i = 0; i < 8; ++i) {
    unsigned x = margin + i % per_row * (card_cols + 2), y = i / per_row * 4;
    device->device_id = i;
    alloc_device_window(y, x, card_cols, 0, &window);
    draw_devices(&devices, &interface);
    doupdate();
    char first[512], second[512], bars[512];
    mvwinnstr(curscr, y, x, first, card_cols);
    mvwinnstr(curscr, y + 1, x, second, card_cols);
    mvwinnstr(curscr, y + 2, x, bars, card_cols);
    assert(strstr(first, "NPU"));
    assert(strstr(second, "TEMP") && strstr(second, "POW"));
    assert(strstr(bars, "NPU[") && strstr(bars, "HBM["));
    if (interface.device_header_cols >= DETAILED_DEVICE_COLS) {
      assert(strstr(first, populated ? "PCIe GEN4@16x" : "PCIe N/A"));
      assert(strstr(first, populated ? "RX 123MB/ms" : "RX N/A"));
      assert(strstr(first, populated ? "TX 0MB/ms" : "TX N/A"));
      assert(strstr(second, populated ? "ECC 1844..." : "ECC N/A/N/A"));
      assert(strstr(second, populated ? "HCCS 7x WARN" : "HCCS N/A N/A"));
      assert(strstr(second, populated ? "RX 1.000MiB/s" : "RX N/A"));
      assert(strstr(second, populated ? "TX 0.000KiB/s" : "TX N/A"));
      if (populated)
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
    if (i % per_row) {
      for (unsigned row = y; row < y + 3; ++row) {
        assert((mvwinch(curscr, row, x - 1) & A_CHARTEXT) == ' ');
        assert((mvwinch(curscr, row, x - 2) & A_CHARTEXT) == ' ');
      }
    }
    free_device_windows(&window);
  }
  list_del(&device->list);
  endwin();
  delscreen(screen);
  fclose(input);
  fclose(output);
}

int main(void) {
  assert(device_length(8, 24, 80, 0, 76, false) == 38);
  assert(device_length(8, 50, 160, 0, 76, false) == 76);
  assert(device_length(8, 50, 312, 0, 76, false) == 76);
  assert(device_length(8, 50, 320, 0, 76, false) == 76);
  assert(device_length(8, 24, 80, 0, 92, true) == 92);
  assert(device_length(8, 18, 160, 0, 76, false) == 38);

  struct gpu_info device = {0};
  struct gpuinfo_dynamic_info *info = &device.dynamic_info;
  char cells[LINK_INFO_FIELDS][LINK_INFO_CELL_SIZE];
  format_link_info(info, cells);
  assert(!strcmp(cells[0], "PCIe N/A"));
  assert(!strcmp(cells[1], "ECC N/A/N/A"));
  assert(!strcmp(cells[2], "RX N/A"));
  assert(!strcmp(cells[3], "TX N/A"));
  assert(!strcmp(cells[4], "HCCS N/A"));
  assert(!strcmp(cells[5], "N/A"));
  assert(!strcmp(cells[6], "RX N/A"));
  assert(!strcmp(cells[7], "TX N/A"));
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
  format_link_info(info, cells);
  assert(!strcmp(cells[0], "PCIe GEN4@16x"));
  assert(!strcmp(cells[1], "ECC 18446744073709551615/4294967296"));
  assert(!strcmp(cells[2], "RX 123MB/ms"));
  assert(!strcmp(cells[3], "TX 0MB/ms"));
  assert(!strcmp(cells[4], "HCCS 7x"));
  assert(!strcmp(cells[5], "WARN"));
  assert(!strcmp(cells[6], "RX 1.000MiB/s"));
  assert(!strcmp(cells[7], "TX 0.000KiB/s"));
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
  fit_link_cell(cells[1], 18);
  assert(strlen(cells[1]) == 18);
  assert(!strncmp(cells[1], "ECC 18446744073", 15));
  assert(!strcmp(cells[1] + 15, "..."));
  fit_link_cell(cells[0], strlen(cells[0]));
  assert(!strcmp(cells[0], "PCIe GEN4@16x"));
  fit_link_cell(cells[0], LINK_INFO_CELL_SIZE);
  assert(!strcmp(cells[0], "PCIe GEN4@16x"));
  fit_link_cell(cells[0], 3);
  assert(!strcmp(cells[0], "..."));

  SET_GPUINFO_DYNAMIC(info, hccs_healthy, true);
  SET_GPUINFO_DYNAMIC(info, hccs_rx_bytes_per_second, 1073741824.);
  format_link_info(info, cells);
  assert(!strcmp(cells[5], "OK"));
  assert(!strcmp(cells[6], "RX 1.000GiB/s"));
  RESET_GPUINFO_DYNAMIC(info, ecc_corrected);
  RESET_GPUINFO_DYNAMIC(info, hccs_rx_bytes_per_second);
  format_link_info(info, cells);
  assert(!strcmp(cells[1], "ECC N/A/4294967296"));
  assert(!strcmp(cells[6], "RX N/A"));
  puts("UI links: missing values, native PCIe units, ECC width, HCCS scales and layout fallback passed");
  return 0;
}
