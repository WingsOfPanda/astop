#ifndef ASCEND_LINK_SAMPLING_H_
#define ASCEND_LINK_SAMPLING_H_

#include "ascend/dcmi_interface_api.h"
#include "nvtop/time.h"

struct gpuinfo_dynamic_info;

struct ascend_link_device {
  int card_id;
  int device_id;
  int logical_id;
  bool logical_id_valid;
  bool use_dcmiv2;
};

/* Implementation-owned storage: zero-initialize once per device, then access
 * only through ascend_link_sample. Embedded to preserve device allocation. */
struct ascend_link_sampler {
  time_t last_pcie_query;
  unsigned pcie_rx;
  unsigned pcie_tx;
  unsigned long long pcie_rx_mb_per_ms;
  unsigned long long pcie_tx_mb_per_ms;
  bool pcie_bandwidth_valid;
  unsigned hccs_port_mask;
  unsigned long long hccs_rx[DCMI_HCCS_MAX_PCS_NUM];
  unsigned long long hccs_tx[DCMI_HCCS_MAX_PCS_NUM];
  nvtop_time hccs_timestamp;
  bool hccs_baseline_valid;
};

/* Refresh only HCCS and PCIe bandwidth fields, including their validity.
 * PCIe profiling is cached for five wall-clock seconds. last_status changes
 * only when a driver call runs, preserving the backend's error reporting. */
void ascend_link_sample(struct ascend_link_sampler *sampler, const struct ascend_link_device *device,
                        struct gpuinfo_dynamic_info *dynamic_info, int *last_status);

#endif
