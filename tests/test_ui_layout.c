#include "nvtop/interface_layout_selection.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition, message)                                               \
  do {                                                                         \
    if (!(condition)) {                                                         \
      fprintf(stderr, "layout %ux%u: %s\n", cols, rows, message);                \
      exit(EXIT_FAILURE);                                                      \
    }                                                                          \
  } while (0)

static void check_layout(unsigned cols, unsigned rows, unsigned per_row, unsigned min_width) {
  nvtop_interface_gpu_opts options[8] = {0};
  struct window_position devices[8], plots[MAX_CHARTS], process, setup;
  unsigned plot_count, map[8];
  for (unsigned i = 0; i < 8; ++i)
    options[i].to_draw = plot_default_draw_info();
  compute_sizes_from_layout(8, 3, min_width, rows - 1, cols, options, process_default_displayed_field(),
                            devices, &plot_count, plots, map, &process, &setup, false);

  unsigned width = (cols - 2 * (per_row - 1)) / per_row;
  unsigned margin = (cols - per_row * width - 2 * (per_row - 1)) / 2;
  CHECK(device_cards_per_row(8, min_width, cols) == per_row, "preflight and card layout must share row geometry");
  fprintf(stderr, "layout %ux%u: first x=%u width=%u, next x=%u; expected x=%u width=%u gutter=2\n",
          cols, rows, devices[0].posX, devices[0].sizeX, devices[1].posX, margin, width);
  CHECK(width >= min_width, "card content must fit its metadata fields");
  for (unsigned i = 0; i < 8; ++i) {
    CHECK(devices[i].sizeX == width, "all cards must use the same available column width");
    CHECK(devices[i].posX == margin + (i % per_row) * (width + 2),
          "card columns must share a centered grid with fixed two-column gutters");
    CHECK(devices[i].posY == devices[i / per_row * per_row].posY, "cards in a row must align");
    CHECK(devices[i].posX + devices[i].sizeX <= cols, "card fields must stay inside the terminal");
    CHECK(devices[i].posY + devices[i].sizeY <= process.posY, "cards must not clip processes");
    if (i >= per_row)
      CHECK(devices[i].posY >= devices[i - per_row].posY + 3, "header rows must not overlap");
  }
  CHECK(process.sizeY >= 3, "eight-device layout must retain a useful process table");
  CHECK(process.posY + process.sizeY < rows, "process table must leave the shortcut row");
}

int main(void) {
  check_layout(312, 50, 4, 76);
  check_layout(80, 24, 2, 38);
  check_layout(160, 50, 2, 76);
  check_layout(304, 50, 3, 76);
  check_layout(312, 50, 4, 76);
  check_layout(320, 50, 4, 76);
  check_layout(321, 50, 4, 76);
  check_layout(320, 50, 3, 92);
  check_layout(160, 50, 1, 92);
  // Re-enter narrower grids after wide layouts, as terminal resize does.
  check_layout(312, 50, 4, 76);
  check_layout(80, 24, 2, 38);
  puts("UI layout: aligned fields, fixed gutters, wrapping and resize passed");
  return 0;
}
