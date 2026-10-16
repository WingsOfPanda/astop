#include "nvtop/extract_gpuinfo.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool init_ok = true;
static bool enumerate_ok = true;
static unsigned shutdown_count;

static bool init(void) { return init_ok; }
static bool enumerate(struct list_head *devices, unsigned *count) {
  (void)devices;
  *count = 0;
  return enumerate_ok;
}
static void shutdown_vendor(void) { ++shutdown_count; }
static const char *error(void) { return "expected test failure"; }

int main(void) {
  struct gpu_vendor vendor = {.init = init,
                              .shutdown = shutdown_vendor,
                              .get_device_handles = enumerate,
                              .last_error_string = error,
                              .name = "test"};
  register_gpu_vendor(&vendor);
  LIST_HEAD(devices);
  unsigned count = 123;
  assert(gpuinfo_init_info_extraction(&count, &devices));
  assert(count == 0 && list_empty(&devices));
  init_ok = false;
  assert(!gpuinfo_init_info_extraction(&count, &devices));
  init_ok = true;
  enumerate_ok = false;
  assert(!gpuinfo_init_info_extraction(&count, &devices));
  assert(shutdown_count == 1);

  struct gpu_info device = {0};
  list_add(&device.list, &devices);
  SET_GPUINFO_DYNAMIC(&device.dynamic_info, gpu_util_rate, 42);
  SET_GPUINFO_DYNAMIC(&device.dynamic_info, power_draw, 100000);
  SET_GPUINFO_DYNAMIC(&device.dynamic_info, power_draw_max, 200000);
  assert(gpuinfo_fix_dynamic_info_from_process_info(&devices));
  assert(device.dynamic_info.gpu_util_rate == 42);
  assert(device.dynamic_info.effective_load_rate == 21);
  assert(!GPUINFO_DYNAMIC_FIELD_VALID(&device.dynamic_info, mem_util_rate));
  RESET_GPUINFO_DYNAMIC(&device.dynamic_info, gpu_util_rate);
  assert(gpuinfo_fix_dynamic_info_from_process_info(&devices));
  assert(!GPUINFO_DYNAMIC_FIELD_VALID(&device.dynamic_info, gpu_util_rate));
  assert(!GPUINFO_DYNAMIC_FIELD_VALID(&device.dynamic_info, effective_load_rate));
  puts("core tests passed");
  return 0;
}
