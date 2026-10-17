/*
 * Copyright (C) 2023 klayer <klayer@163.com>
 *
 * This file is part of Nvtop and adapted from Ascend DCMI from Huawei Technologies Co., Ltd.
 *
 * Nvtop is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Nvtop is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with nvtop.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ascend/link_sampling.h"
#include "nvtop/extract_gpuinfo_common.h"

#define DCMI_SUCCESS 0
#define ASCEND_PROF_DATA_NUM 3

/* Profiling may block in the driver; sample briefly and reuse between queries. */
#define ASCEND_PCIE_PROFILING_TIME_MS 100
#ifndef ASCEND_PCIE_QUERY_INTERVAL_SEC
#define ASCEND_PCIE_QUERY_INTERVAL_SEC 5
#endif

/* Supports both strong CANN declarations and absent weak exports. */
#define DCMI_SYMBOL_PRESENT(symbol) ((uintptr_t)(symbol) != (uintptr_t)0)

static int ascend_get_hccs_info(const struct ascend_link_device *gpu, int *last_status, unsigned sub_cmd,
                                void *buf, unsigned *size) {
  if (gpu->use_dcmiv2 && gpu->logical_id_valid && DCMI_SYMBOL_PRESENT(dcmiv2_get_device_info)) {
    *last_status = dcmiv2_get_device_info(gpu->logical_id, DCMI_MAIN_CMD_HCCS, sub_cmd, buf, size);
    if (*last_status == DCMI_SUCCESS)
      return *last_status;
  }
  if (gpu->card_id < 0 || gpu->device_id < 0 || !DCMI_SYMBOL_PRESENT(dcmi_get_device_info))
    return -1;
  return (*last_status = dcmi_get_device_info(gpu->card_id, gpu->device_id, DCMI_MAIN_CMD_HCCS, sub_cmd, buf, size));
}

static void ascend_update_hccs_rates(struct ascend_link_sampler *gpu, struct gpuinfo_dynamic_info *dynamic_info,
                                     const struct dcmi_hccs_statistic_info_u64 *stats, unsigned port_mask,
                                     nvtop_time now) {
  double elapsed = nvtop_difftime(gpu->hccs_timestamp, now);
  bool valid = gpu->hccs_baseline_valid && port_mask && port_mask == gpu->hccs_port_mask && elapsed > 0.;
  double rx_flits = 0.;
  double tx_flits = 0.;
  RESET_GPUINFO_DYNAMIC(dynamic_info, hccs_rx_bytes_per_second);
  RESET_GPUINFO_DYNAMIC(dynamic_info, hccs_tx_bytes_per_second);
  for (unsigned port = 0; port < DCMI_HCCS_MAX_PCS_NUM; ++port) {
    if (!(port_mask & (1u << port)))
      continue;
    if (stats->rx_cnt[port] < gpu->hccs_rx[port] || stats->tx_cnt[port] < gpu->hccs_tx[port])
      valid = false;
    if (valid) {
      rx_flits += stats->rx_cnt[port] - gpu->hccs_rx[port];
      tx_flits += stats->tx_cnt[port] - gpu->hccs_tx[port];
    }
    gpu->hccs_rx[port] = stats->rx_cnt[port];
    gpu->hccs_tx[port] = stats->tx_cnt[port];
  }
  gpu->hccs_port_mask = port_mask;
  gpu->hccs_timestamp = now;
  gpu->hccs_baseline_valid = port_mask != 0;
  if (valid) {
    /* HCCS counters count 20-byte flits; subtract before converting to double. */
    SET_GPUINFO_DYNAMIC(dynamic_info, hccs_rx_bytes_per_second, rx_flits * 20. / elapsed);
    SET_GPUINFO_DYNAMIC(dynamic_info, hccs_tx_bytes_per_second, tx_flits * 20. / elapsed);
  }
}

static void ascend_refresh_hccs(struct ascend_link_sampler *gpu, const struct ascend_link_device *device,
                                struct gpuinfo_dynamic_info *dynamic_info, int *last_status) {
  struct dcmi_hccs_lane_info lanes = {0};
  struct dcmi_hccs_statues status = {0};
  struct dcmi_hccs_statistic_info_u64 stats = {0};
  unsigned size = sizeof(lanes);
  RESET_GPUINFO_DYNAMIC(dynamic_info, hccs_links);
  RESET_GPUINFO_DYNAMIC(dynamic_info, hccs_healthy);
  RESET_GPUINFO_DYNAMIC(dynamic_info, hccs_rx_bytes_per_second);
  RESET_GPUINFO_DYNAMIC(dynamic_info, hccs_tx_bytes_per_second);
  if (ascend_get_hccs_info(device, last_status, DCMI_HCCS_CMD_GET_LANE_INFO, &lanes, &size) != DCMI_SUCCESS ||
      size < sizeof(lanes.hccs_port_pcs_bitmap))
    goto unavailable;
  unsigned port_mask = lanes.hccs_port_pcs_bitmap & ((1u << DCMI_HCCS_MAX_PCS_NUM) - 1u);
  unsigned links = 0;
  for (unsigned port = 0; port < DCMI_HCCS_MAX_PCS_NUM; ++port)
    links += (port_mask >> port) & 1u;
  SET_GPUINFO_DYNAMIC(dynamic_info, hccs_links, links);

  size = sizeof(status);
  if (ascend_get_hccs_info(device, last_status, DCMI_HCCS_CMD_GET_STATUS, &status, &size) != DCMI_SUCCESS ||
      size < offsetof(struct dcmi_hccs_statues, reserve))
    goto unavailable;
  SET_GPUINFO_DYNAMIC(dynamic_info, hccs_healthy, status.pcs_status == 0 && (status.hdlc_status & 3u) == 3u);
  if (!port_mask)
    goto unavailable;

  size = sizeof(stats);
  if (ascend_get_hccs_info(device, last_status, DCMI_HCCS_CMD_GET_STATISTIC_INFO_U64, &stats, &size) !=
          DCMI_SUCCESS ||
      size < offsetof(struct dcmi_hccs_statistic_info_u64, crc_err_cnt))
    goto unavailable;
  nvtop_time now;
  nvtop_get_current_time(&now);
  ascend_update_hccs_rates(gpu, dynamic_info, &stats, port_mask, now);
  return;

unavailable:
  gpu->hccs_baseline_valid = false;
}

static unsigned ascend_bandwidth_to_kb_per_second(uint64_t average) {
  /* shortcut: inherited binary scaling estimates PCIe plot load; verify native MB units before changing it. */
  if (average > UINT64_MAX / UINT64_C(1024000))
    return UINT_MAX;
  average *= UINT64_C(1024000); /* 1024 KiB/MiB * 1000 ms/s */
  return average > UINT_MAX ? UINT_MAX : (unsigned)average;
}

static void ascend_refresh_pcie_bandwidth_cache(struct ascend_link_sampler *gpu_info,
                                                 const struct ascend_link_device *device, int *last_status) {
  time_t now;
  bool should_query;

  if (!DCMI_SYMBOL_PRESENT(dcmi_get_pcie_link_bandwidth_info) &&
      !(device->use_dcmiv2 && DCMI_SYMBOL_PRESENT(dcmiv2_get_pcie_link_bandwidth_info)))
    return;

  now = time(NULL);
  should_query = gpu_info->last_pcie_query == (time_t)0 || now < gpu_info->last_pcie_query ||
                 (unsigned long long)(now - gpu_info->last_pcie_query) >= ASCEND_PCIE_QUERY_INTERVAL_SEC;
  if (should_query) {
    gpu_info->pcie_bandwidth_valid = false;
    struct dcmi_pcie_link_bandwidth_info bandwidth;
    memset(&bandwidth, 0, sizeof(bandwidth));
    bandwidth.profiling_time = ASCEND_PCIE_PROFILING_TIME_MS;
    if (device->use_dcmiv2 && device->logical_id_valid && DCMI_SYMBOL_PRESENT(dcmiv2_get_pcie_link_bandwidth_info)) {
      *last_status = dcmiv2_get_pcie_link_bandwidth_info(device->logical_id, &bandwidth);
    } else if (device->card_id >= 0 && device->device_id >= 0 && DCMI_SYMBOL_PRESENT(dcmi_get_pcie_link_bandwidth_info)) {
      *last_status = dcmi_get_pcie_link_bandwidth_info(device->card_id, device->device_id, &bandwidth);
    } else {
      return;
    }
    gpu_info->last_pcie_query = now;
    if (*last_status == DCMI_SUCCESS) {
      /* DCMI arrays are [min, max, average]; preserve their documented MB/ms unit. */
      gpu_info->pcie_tx_mb_per_ms =
          (uint64_t)bandwidth.tx_p_bw[ASCEND_PROF_DATA_NUM - 1] + bandwidth.tx_np_bw[ASCEND_PROF_DATA_NUM - 1] +
          bandwidth.tx_cpl_bw[ASCEND_PROF_DATA_NUM - 1];
      gpu_info->pcie_rx_mb_per_ms =
          (uint64_t)bandwidth.rx_p_bw[ASCEND_PROF_DATA_NUM - 1] + bandwidth.rx_np_bw[ASCEND_PROF_DATA_NUM - 1] +
          bandwidth.rx_cpl_bw[ASCEND_PROF_DATA_NUM - 1];
      gpu_info->pcie_tx = ascend_bandwidth_to_kb_per_second(gpu_info->pcie_tx_mb_per_ms);
      gpu_info->pcie_rx = ascend_bandwidth_to_kb_per_second(gpu_info->pcie_rx_mb_per_ms);
      gpu_info->pcie_bandwidth_valid = true;
    }
  }
}

void ascend_link_sample(struct ascend_link_sampler *sampler, const struct ascend_link_device *device,
                        struct gpuinfo_dynamic_info *dynamic_info, int *last_status) {
  ascend_refresh_hccs(sampler, device, dynamic_info, last_status);
  RESET_GPUINFO_DYNAMIC(dynamic_info, pcie_rx);
  RESET_GPUINFO_DYNAMIC(dynamic_info, pcie_tx);
  RESET_GPUINFO_DYNAMIC(dynamic_info, pcie_rx_mb_per_ms);
  RESET_GPUINFO_DYNAMIC(dynamic_info, pcie_tx_mb_per_ms);
  ascend_refresh_pcie_bandwidth_cache(sampler, device, last_status);
  if (sampler->pcie_bandwidth_valid) {
    SET_GPUINFO_DYNAMIC(dynamic_info, pcie_rx, sampler->pcie_rx);
    SET_GPUINFO_DYNAMIC(dynamic_info, pcie_tx, sampler->pcie_tx);
    SET_GPUINFO_DYNAMIC(dynamic_info, pcie_rx_mb_per_ms, sampler->pcie_rx_mb_per_ms);
    SET_GPUINFO_DYNAMIC(dynamic_info, pcie_tx_mb_per_ms, sampler->pcie_tx_mb_per_ms);
  }
}
