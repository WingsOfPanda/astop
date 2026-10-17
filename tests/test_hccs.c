#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "ascend/link_sampling.h"
#include "nvtop/extract_gpuinfo_common.h"

static struct dcmi_hccs_statistic_info_u64 sample;
static unsigned sample_mask = 0xfe;
static unsigned sample_pcs;
static unsigned sample_hdlc = 3;
static unsigned stats_size = 896;
static unsigned lanes_size = sizeof(struct dcmi_hccs_lane_info);
static unsigned status_size = sizeof(struct dcmi_hccs_statues);
static int failed_command = -1;
static unsigned pcie_average = 2;
static int pcie_status;
static unsigned legacy_hccs_calls, legacy_pcie_calls;
#ifndef ASCEND_TEST_LEGACY_ONLY
static int v2_status;
static unsigned v2_hccs_calls, v2_pcie_calls;
#endif
static nvtop_time monotonic_now;
static time_t wall_now = 100;

/* Link-time test adapter: the production module still uses real DCMI/clocks. */
time_t time(time_t *result) {
  if (result)
    *result = wall_now;
  return wall_now;
}

int clock_gettime(clockid_t clock, struct timespec *result) {
  assert(clock == NVTOP_CLOCK);
  *result = monotonic_now;
  return 0;
}

static int get_bandwidth(struct dcmi_pcie_link_bandwidth_info *bandwidth) {
  assert(bandwidth->profiling_time == 100);
  if (pcie_status)
    return pcie_status;
  bandwidth->tx_p_bw[2] = pcie_average;
  bandwidth->tx_np_bw[2] = pcie_average;
  bandwidth->tx_cpl_bw[2] = pcie_average;
  bandwidth->rx_p_bw[2] = pcie_average;
  bandwidth->rx_np_bw[2] = pcie_average;
  return 0;
}

int dcmi_get_pcie_link_bandwidth_info(int card_id, int device_id, struct dcmi_pcie_link_bandwidth_info *bandwidth) {
  assert(card_id == 0 && device_id == 0);
  ++legacy_pcie_calls;
  return get_bandwidth(bandwidth);
}

static int get_device_info(enum dcmi_main_cmd main_cmd, unsigned sub_cmd, void *buf, unsigned *size) {
  assert(main_cmd == DCMI_MAIN_CMD_HCCS);
  if ((int)sub_cmd == failed_command)
    return -7;
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
  return 0;
}

int dcmi_get_device_info(int card_id, int device_id, enum dcmi_main_cmd main_cmd, unsigned sub_cmd, void *buf,
                         unsigned *size) {
  assert(card_id == 0 && device_id == 0);
  ++legacy_hccs_calls;
  return get_device_info(main_cmd, sub_cmd, buf, size);
}

#ifndef ASCEND_TEST_LEGACY_ONLY
int dcmiv2_get_device_info(int logical_id, enum dcmi_main_cmd main_cmd, unsigned sub_cmd, void *buf, unsigned *size) {
  assert(logical_id == 42);
  ++v2_hccs_calls;
  return v2_status ? v2_status : get_device_info(main_cmd, sub_cmd, buf, size);
}

int dcmiv2_get_pcie_link_bandwidth_info(int logical_id, struct dcmi_pcie_link_bandwidth_info *bandwidth) {
  assert(logical_id == 42);
  ++v2_pcie_calls;
  return v2_status ? v2_status : get_bandwidth(bandwidth);
}
#endif

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

static void assert_pcie(const struct gpuinfo_dynamic_info *dynamic, unsigned long long rx, unsigned long long tx,
                        unsigned plot_rx, unsigned plot_tx) {
  assert(GPUINFO_DYNAMIC_FIELD_VALID(dynamic, pcie_rx_mb_per_ms));
  assert(GPUINFO_DYNAMIC_FIELD_VALID(dynamic, pcie_tx_mb_per_ms));
  assert(GPUINFO_DYNAMIC_FIELD_VALID(dynamic, pcie_rx));
  assert(GPUINFO_DYNAMIC_FIELD_VALID(dynamic, pcie_tx));
  assert(dynamic->pcie_rx_mb_per_ms == rx && dynamic->pcie_tx_mb_per_ms == tx);
  assert(dynamic->pcie_rx == plot_rx && dynamic->pcie_tx == plot_tx);
}

static void assert_pcie_unavailable(const struct gpuinfo_dynamic_info *dynamic) {
  assert(!GPUINFO_DYNAMIC_FIELD_VALID(dynamic, pcie_rx_mb_per_ms));
  assert(!GPUINFO_DYNAMIC_FIELD_VALID(dynamic, pcie_tx_mb_per_ms));
  assert(!GPUINFO_DYNAMIC_FIELD_VALID(dynamic, pcie_rx));
  assert(!GPUINFO_DYNAMIC_FIELD_VALID(dynamic, pcie_tx));
}

static const struct ascend_link_device legacy = {.card_id = 0, .device_id = 0};
static struct ascend_link_sampler sampler;
static struct gpuinfo_dynamic_info dynamic;
static int last_status;

static void sample_at(time_t seconds, long nanoseconds) {
  monotonic_now = (nvtop_time){seconds, nanoseconds};
  ascend_link_sample(&sampler, &legacy, &dynamic, &last_status);
  assert(GPUINFO_DYNAMIC_FIELD_VALID(&dynamic, ecc_corrected) && dynamic.ecc_corrected == 42);
}

static void test_hccs(void) {
  sample.rx_cnt[1] = (1ull << 55);
  sample.tx_cnt[1] = (1ull << 55);
  sample_at(1, 0);
  assert_unavailable(&dynamic);
  assert(GPUINFO_DYNAMIC_FIELD_VALID(&dynamic, hccs_links) && dynamic.hccs_links == 7);
  assert(GPUINFO_DYNAMIC_FIELD_VALID(&dynamic, hccs_healthy) && dynamic.hccs_healthy);
  sample_at(2, 0); /* 896-byte response establishes a usable baseline. */
  assert_rate(&dynamic, 0., 0.);
  sample.rx_cnt[1] += 1;
  sample.tx_cnt[1] += 2;
  sample.rx_cnt[7] += 3;
  sample.tx_cnt[7] += 5;
  sample.rx_cnt[0] += 1000; /* Port zero is outside the configured mask. */
  sample_at(2, 500000000);
  assert_rate(&dynamic, 160., 280.);

  sample.rx_cnt[1] = 0; /* A per-port reset invalidates both directions. */
  sample.rx_cnt[7] += (1ull << 56);
  sample_at(3, 0);
  assert_unavailable(&dynamic);
  sample_at(4, 0);
  assert_rate(&dynamic, 0., 0.);
  sample_at(4, 0);
  assert_unavailable(&dynamic);
  sample_at(3, 0);
  assert_unavailable(&dynamic);
  sample_mask = 0x6;
  sample_at(5, 0);
  assert_unavailable(&dynamic);
  sample_mask = 0;
  sample_at(6, 0);
  assert_unavailable(&dynamic);
  assert(GPUINFO_DYNAMIC_FIELD_VALID(&dynamic, hccs_links) && dynamic.hccs_links == 0);
  sample_mask = 0xfe;
  sample_at(7, 0);
  assert_unavailable(&dynamic);
  sample_at(8, 0);
  assert_rate(&dynamic, 0., 0.);

  failed_command = DCMI_HCCS_CMD_GET_STATISTIC_INFO_U64;
  sample_at(9, 0);
  assert_unavailable(&dynamic);
  assert(last_status == -7);
  assert_pcie(&dynamic, 4, 6, 4096000, 6144000);
  failed_command = -1;
  sample_at(10, 0);
  assert_unavailable(&dynamic);
  sample_at(11, 0);
  assert_rate(&dynamic, 0., 0.);

  stats_size = offsetof(struct dcmi_hccs_statistic_info_u64, crc_err_cnt) - 1;
  sample_at(12, 0);
  assert_unavailable(&dynamic);
  stats_size = 896;
  sample_at(13, 0);
  assert_unavailable(&dynamic);
  sample_at(14, 0);
  assert_rate(&dynamic, 0., 0.);
  lanes_size = 3;
  sample_at(15, 0);
  assert(!GPUINFO_DYNAMIC_FIELD_VALID(&dynamic, hccs_links));
  assert(!GPUINFO_DYNAMIC_FIELD_VALID(&dynamic, hccs_healthy));
  assert_unavailable(&dynamic);
  lanes_size = sizeof(struct dcmi_hccs_lane_info);
  status_size = offsetof(struct dcmi_hccs_statues, reserve) - 1;
  sample_at(16, 0);
  assert(GPUINFO_DYNAMIC_FIELD_VALID(&dynamic, hccs_links));
  assert(!GPUINFO_DYNAMIC_FIELD_VALID(&dynamic, hccs_healthy));
  assert_unavailable(&dynamic);
  status_size = sizeof(struct dcmi_hccs_statues);
  sample_pcs = 1;
  sample_at(17, 0);
  assert(GPUINFO_DYNAMIC_FIELD_VALID(&dynamic, hccs_healthy) && !dynamic.hccs_healthy);
  sample_pcs = 0;
  sample_hdlc = 7;
  sample_at(18, 0);
  assert(GPUINFO_DYNAMIC_FIELD_VALID(&dynamic, hccs_healthy) && dynamic.hccs_healthy);
  sample_hdlc = 0;
  sample_at(19, 0);
  assert(GPUINFO_DYNAMIC_FIELD_VALID(&dynamic, hccs_healthy) && !dynamic.hccs_healthy);
  sample_mask = 1u << 20; /* Unsupported bitmap bits do not become links. */
  sample_at(20, 0);
  assert(GPUINFO_DYNAMIC_FIELD_VALID(&dynamic, hccs_links) && dynamic.hccs_links == 0);
  assert_unavailable(&dynamic);
  sample_mask = 0xfe;
  sample_hdlc = 3;
}

static void test_pcie_cache(void) {
  assert(legacy_pcie_calls == 1);
  assert_pcie(&dynamic, 4, 6, 4096000, 6144000);
  pcie_average = UINT_MAX;
  wall_now = 104;
  sample_at(21, 0);
  assert(legacy_pcie_calls == 1);
  assert_pcie(&dynamic, 4, 6, 4096000, 6144000);
  wall_now = 105; /* Expiry is inclusive. */
  sample_at(22, 0);
  assert(legacy_pcie_calls == 2);
  assert_pcie(&dynamic, (uint64_t)UINT_MAX * 2, (uint64_t)UINT_MAX * 3, UINT_MAX, UINT_MAX);
  wall_now = 103; /* A backwards wall clock forces a fresh query. */
  pcie_average = 0;
  sample_at(23, 0);
  assert(legacy_pcie_calls == 3);
  assert_pcie(&dynamic, 0, 0, 0, 0);
  wall_now = 108;
  pcie_status = -9;
  sample_at(24, 0);
  assert(legacy_pcie_calls == 4);
  assert(last_status == -9);
  assert_pcie_unavailable(&dynamic);
  assert_rate(&dynamic, 0., 0.);
  pcie_status = 0;
  pcie_average = 1;
  wall_now = 112; /* Failures retain the same retry cadence. */
  sample_at(25, 0);
  assert(legacy_pcie_calls == 4);
  assert_pcie_unavailable(&dynamic);
  wall_now = 113;
  sample_at(26, 0);
  assert(legacy_pcie_calls == 5);
  assert_pcie(&dynamic, 2, 3, 2048000, 3072000);

  struct ascend_link_sampler other = {0};
  ascend_link_sample(&other, &legacy, &dynamic, &last_status);
  assert(legacy_pcie_calls == 6); /* Independent device state queries immediately. */
  assert_unavailable(&dynamic);
}

static void test_driver_selection(void) {
  struct ascend_link_device logical = {.card_id = 0, .device_id = 0, .logical_id = 42,
                                       .logical_id_valid = true, .use_dcmiv2 = true};
  struct ascend_link_sampler logical_sampler = {0};
  unsigned legacy_calls = legacy_hccs_calls;
  unsigned legacy_pcie = legacy_pcie_calls;
  ascend_link_sample(&logical_sampler, &logical, &dynamic, &last_status);
#ifdef ASCEND_TEST_LEGACY_ONLY
  assert(legacy_hccs_calls == legacy_calls + 3);
  assert(legacy_pcie_calls == legacy_pcie + 1);
#else
  assert(legacy_hccs_calls == legacy_calls && v2_hccs_calls == 3);
  assert(legacy_pcie_calls == legacy_pcie && v2_pcie_calls == 1);
  v2_status = -11;
  wall_now += 5;
  ascend_link_sample(&logical_sampler, &logical, &dynamic, &last_status);
  assert(legacy_hccs_calls == legacy_calls + 3); /* HCCS falls back after a v2 error. */
  assert(legacy_pcie_calls == legacy_pcie); /* PCIe preserves its no-fallback behavior. */
  assert_pcie_unavailable(&dynamic);
  assert(last_status == -11);
  v2_status = 0;
  wall_now += 5;
  ascend_link_sample(&logical_sampler, &logical, &dynamic, &last_status);
  assert_pcie(&dynamic, 2, 3, 2048000, 3072000);

  logical.card_id = logical.device_id = -1;
  struct ascend_link_sampler v2_only = {0};
  ascend_link_sample(&v2_only, &logical, &dynamic, &last_status);
  assert(GPUINFO_DYNAMIC_FIELD_VALID(&dynamic, hccs_links));
  assert_pcie(&dynamic, 2, 3, 2048000, 3072000);
#endif

  logical.card_id = logical.device_id = -1;
  logical.logical_id_valid = false;
  struct ascend_link_sampler unavailable = {0};
  last_status = 37;
  ascend_link_sample(&unavailable, &logical, &dynamic, &last_status);
  assert(!GPUINFO_DYNAMIC_FIELD_VALID(&dynamic, hccs_links));
  assert(!GPUINFO_DYNAMIC_FIELD_VALID(&dynamic, hccs_healthy));
  assert_unavailable(&dynamic);
  assert_pcie_unavailable(&dynamic);
  assert(last_status == 37); /* No driver call means no invented driver status. */
}

int main(void) {
  _Static_assert(sizeof(struct dcmi_hccs_statues) == 12, "status ABI");
  _Static_assert(sizeof(struct dcmi_hccs_lane_info) == 132, "lanes ABI");
  _Static_assert(sizeof(struct dcmi_hccs_statistic_info_u64) == 1024, "stats ABI");
  SET_GPUINFO_DYNAMIC(&dynamic, ecc_corrected, 42);
  test_hccs();
  test_pcie_cache();
  test_driver_selection();
  puts("HCCS and PCIe sampling tests passed");
  return 0;
}
