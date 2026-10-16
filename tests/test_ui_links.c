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

int main(void) {
  assert(link_rows_for_layout(8, 24, 80, 0, false) == 0);
  assert(link_rows_for_layout(8, 50, 160, 0, false) == LINK_INFO_ROWS);
  assert(link_rows_for_layout(8, 50, 320, 0, false) == LINK_INFO_ROWS);
  assert(link_rows_for_layout(8, 24, 80, 1, true) == LINK_INFO_ROWS);
  assert(link_rows_for_layout(1, 24, 80, 1, true) == LINK_INFO_ROWS);
  // Returning from a wide layout to a short terminal must shed automatic rows.
  assert(link_rows_for_layout(8, 20, 160, 0, false) == 0);

  struct gpu_info device = {0};
  struct gpuinfo_dynamic_info *info = &device.dynamic_info;
  char cells[LINK_INFO_ROWS * 2][LINK_INFO_CELL_SIZE];
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
