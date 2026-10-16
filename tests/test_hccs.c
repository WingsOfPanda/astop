#include <assert.h>
#include <math.h>

#include "../src/extract_gpuinfo_ascend.c"

static struct dcmi_hccs_statistic_info_u64 sample;
static unsigned sample_mask = 0xfe;
static unsigned sample_pcs;
static unsigned sample_hdlc = 3;
static unsigned stats_size = 896;
static unsigned lanes_size = sizeof(struct dcmi_hccs_lane_info);
static unsigned status_size = sizeof(struct dcmi_hccs_statues);
static int failed_command = -1;
static unsigned pcie_average = 2;
static bool pcie_query_ok = true;

int dcmi_get_pcie_link_bandwidth_info(int card_id, int device_id, struct dcmi_pcie_link_bandwidth_info *bandwidth) {
  assert(card_id == 0 && device_id == 0);
  assert(bandwidth->profiling_time == ASCEND_PCIE_PROFILING_TIME_MS);
  if (!pcie_query_ok)
    return -1;
  bandwidth->tx_p_bw[2] = pcie_average;
  bandwidth->tx_np_bw[2] = pcie_average;
  bandwidth->tx_cpl_bw[2] = pcie_average;
  bandwidth->rx_p_bw[2] = pcie_average;
  bandwidth->rx_np_bw[2] = pcie_average;
  return DCMI_SUCCESS;
}

int dcmi_get_device_info(int card_id, int device_id, enum dcmi_main_cmd main_cmd, unsigned sub_cmd, void *buf,
                         unsigned *size) {
  assert(card_id == 0 && device_id == 0 && main_cmd == DCMI_MAIN_CMD_HCCS);
  if ((int)sub_cmd == failed_command)
    return -1;
  switch (sub_cmd) {
  case DCMI_HCCS_CMD_GET_LANE_INFO:
    ((struct dcmi_hccs_lane_info *)buf)->hccs_port_pcs_bitmap = sample_mask;
    *size = lanes_size;
    break;
  case DCMI_HCCS_CMD_GET_STATUS:
    ((struct dcmi_hccs_statues *)buf)->pcs_status = sample_pcs;
    ((struct dcmi_hccs_statues *)buf)->hdlc_status = sample_hdlc;
    *size = status_size;
    break;
  case DCMI_HCCS_CMD_GET_STATISTIC_INFO_U64:
    assert(*size == sizeof(sample));
    memcpy(buf, &sample, sizeof(sample));
    *size = stats_size;
    break;
  default:
    assert(false);
  }
  return DCMI_SUCCESS;
}

static void assert_rate(const struct gpuinfo_dynamic_info *dynamic, double rx, double tx) {
  assert(GPUINFO_DYNAMIC_FIELD_VALID(dynamic, hccs_rx_bytes_per_second));
  assert(GPUINFO_DYNAMIC_FIELD_VALID(dynamic, hccs_tx_bytes_per_second));
  assert(fabs(dynamic->hccs_rx_bytes_per_second - rx) < 0.000001);
  assert(fabs(dynamic->hccs_tx_bytes_per_second - tx) < 0.000001);
}

static void assert_unavailable(const struct gpuinfo_dynamic_info *dynamic) {
  assert(!GPUINFO_DYNAMIC_FIELD_VALID(dynamic, hccs_rx_bytes_per_second));
  assert(!GPUINFO_DYNAMIC_FIELD_VALID(dynamic, hccs_tx_bytes_per_second));
}

int main(void) {
  _Static_assert(sizeof(struct dcmi_hccs_statues) == 12, "status ABI");
  _Static_assert(sizeof(struct dcmi_hccs_lane_info) == 132, "lanes ABI");
  _Static_assert(sizeof(struct dcmi_hccs_statistic_info_u64) == 1024, "stats ABI");
  struct gpu_info_ascend gpu = {.card_id = 0, .device_id = 0};
  struct gpuinfo_dynamic_info dynamic = {0};
  sample.rx_cnt[1] = (1ull << 55);
  sample.tx_cnt[1] = (1ull << 55);
  ascend_update_hccs_rates(&gpu, &dynamic, &sample, 0xfe, (nvtop_time){1, 0});
  assert_unavailable(&dynamic);
  ascend_update_hccs_rates(&gpu, &dynamic, &sample, 0xfe, (nvtop_time){2, 0});
  assert_rate(&dynamic, 0., 0.);
  sample.rx_cnt[1] += 1;
  sample.tx_cnt[1] += 2;
  sample.rx_cnt[7] += 3;
  sample.tx_cnt[7] += 5;
  sample.rx_cnt[0] += 1000; /* Port zero is outside the configured mask. */
  ascend_update_hccs_rates(&gpu, &dynamic, &sample, 0xfe, (nvtop_time){2, 500000000});
  assert_rate(&dynamic, 160., 280.);

  sample.rx_cnt[1] = 0; /* A per-port reset invalidates both directions. */
  sample.rx_cnt[7] += (1ull << 56);
  ascend_update_hccs_rates(&gpu, &dynamic, &sample, 0xfe, (nvtop_time){3, 0});
  assert_unavailable(&dynamic);
  ascend_update_hccs_rates(&gpu, &dynamic, &sample, 0xfe, (nvtop_time){4, 0});
  assert_rate(&dynamic, 0., 0.);
  ascend_update_hccs_rates(&gpu, &dynamic, &sample, 0xfe, (nvtop_time){4, 0});
  assert_unavailable(&dynamic);
  ascend_update_hccs_rates(&gpu, &dynamic, &sample, 0xfe, (nvtop_time){3, 0});
  assert_unavailable(&dynamic);
  ascend_update_hccs_rates(&gpu, &dynamic, &sample, 0x6, (nvtop_time){5, 0});
  assert_unavailable(&dynamic);
  ascend_update_hccs_rates(&gpu, &dynamic, &sample, 0, (nvtop_time){6, 0});
  assert_unavailable(&dynamic);
  assert(!gpu.hccs_baseline_valid);

  ascend_refresh_hccs(&gpu, &dynamic);
  assert(GPUINFO_DYNAMIC_FIELD_VALID(&dynamic, hccs_links) && dynamic.hccs_links == 7);
  assert(GPUINFO_DYNAMIC_FIELD_VALID(&dynamic, hccs_healthy) && dynamic.hccs_healthy);
  assert_unavailable(&dynamic);
  assert(gpu.hccs_baseline_valid); /* 896-byte driver response is accepted. */
  gpu.hccs_timestamp.tv_sec -= 1;
  ascend_refresh_hccs(&gpu, &dynamic);
  assert_rate(&dynamic, 0., 0.);
  failed_command = DCMI_HCCS_CMD_GET_STATISTIC_INFO_U64;
  ascend_refresh_hccs(&gpu, &dynamic);
  assert_unavailable(&dynamic);
  assert(!gpu.hccs_baseline_valid);
  failed_command = -1;
  ascend_refresh_hccs(&gpu, &dynamic);
  assert_unavailable(&dynamic);
  assert(gpu.hccs_baseline_valid);
  gpu.hccs_timestamp.tv_sec -= 1;
  ascend_refresh_hccs(&gpu, &dynamic);
  assert_rate(&dynamic, 0., 0.);

  stats_size = offsetof(struct dcmi_hccs_statistic_info_u64, crc_err_cnt) - 1;
  ascend_refresh_hccs(&gpu, &dynamic);
  assert_unavailable(&dynamic);
  assert(!gpu.hccs_baseline_valid);
  stats_size = 896;
  lanes_size = 3;
  ascend_refresh_hccs(&gpu, &dynamic);
  assert(!GPUINFO_DYNAMIC_FIELD_VALID(&dynamic, hccs_links));
  assert(!GPUINFO_DYNAMIC_FIELD_VALID(&dynamic, hccs_healthy));
  lanes_size = sizeof(struct dcmi_hccs_lane_info);
  status_size = offsetof(struct dcmi_hccs_statues, reserve) - 1;
  ascend_refresh_hccs(&gpu, &dynamic);
  assert(!GPUINFO_DYNAMIC_FIELD_VALID(&dynamic, hccs_healthy));
  status_size = sizeof(struct dcmi_hccs_statues);
  sample_pcs = 1;
  ascend_refresh_hccs(&gpu, &dynamic);
  assert(GPUINFO_DYNAMIC_FIELD_VALID(&dynamic, hccs_healthy) && !dynamic.hccs_healthy);
  sample_pcs = 0;
  sample_hdlc = 7;
  ascend_refresh_hccs(&gpu, &dynamic);
  assert(GPUINFO_DYNAMIC_FIELD_VALID(&dynamic, hccs_healthy) && dynamic.hccs_healthy);
  sample_hdlc = 0;
  ascend_refresh_hccs(&gpu, &dynamic);
  assert(GPUINFO_DYNAMIC_FIELD_VALID(&dynamic, hccs_healthy) && !dynamic.hccs_healthy);
  sample_mask = 1u << 20; /* Unsupported bitmap bits do not become links. */
  ascend_refresh_hccs(&gpu, &dynamic);
  assert(GPUINFO_DYNAMIC_FIELD_VALID(&dynamic, hccs_links) && dynamic.hccs_links == 0);
  assert_unavailable(&dynamic);
  assert(!gpu.hccs_baseline_valid);

  ascend_refresh_pcie_bandwidth_cache(&gpu);
  assert(gpu.pcie_bandwidth_valid);
  assert(gpu.pcie_rx_mb_per_ms == 4 && gpu.pcie_tx_mb_per_ms == 6);
  assert(gpu.pcie_rx == 4096000 && gpu.pcie_tx == 6144000);
  pcie_average = UINT_MAX;
  gpu.last_pcie_query = 0;
  ascend_refresh_pcie_bandwidth_cache(&gpu);
  assert(gpu.pcie_rx_mb_per_ms == (uint64_t)UINT_MAX * 2);
  assert(gpu.pcie_tx_mb_per_ms == (uint64_t)UINT_MAX * 3);
  assert(gpu.pcie_rx == UINT_MAX && gpu.pcie_tx == UINT_MAX);
  pcie_query_ok = false;
  gpu.last_pcie_query = 0;
  ascend_refresh_pcie_bandwidth_cache(&gpu);
  assert(!gpu.pcie_bandwidth_valid);
  puts("HCCS tests passed");
  return 0;
}
