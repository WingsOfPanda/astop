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

static void check_process_layout(unsigned rows, bool hide, process_field_displayed fields, unsigned process_height) {
  const unsigned cols = 92;
  nvtop_interface_gpu_opts options[7] = {0};
  struct window_position devices[7], plots[MAX_CHARTS], process, setup;
  unsigned plot_count, map[7];
  for (unsigned i = 0; i < 7; ++i)
    options[i].to_draw = plot_default_draw_info();
  compute_sizes_from_layout(7, 3, 92, rows - 1, cols, options, fields, devices, &plot_count, plots, map,
                            &process, &setup, hide);

  CHECK(process.sizeY == process_height, "unexpected process rectangle height");
  if (process_height)
    CHECK(process.posY == 21 && process.posY + process.sizeY == rows - 1,
          "visible processes must sit between the cards and the shortcut row");
  for (unsigned i = 0; i < 7; ++i) {
    CHECK(devices[i].posX == 0 && devices[i].sizeX == cols && devices[i].sizeY == 3,
          "clock cards must retain their full width and height");
    CHECK(devices[i].posY == i * 3 && devices[i].posY + devices[i].sizeY <= rows - 1,
          "clock cards must stack without overlap and leave the shortcut row");
  }
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
  check_layout(160, 50, 1, 82);
  check_layout(312, 50, 3, 82);
  check_layout(320, 50, 3, 82);
  check_layout(334, 50, 4, 82);
  // Re-enter narrower grids after wide layouts, as terminal resize does.
  check_layout(312, 50, 4, 76);
  check_layout(80, 24, 2, 38);
  process_field_displayed fields = process_default_displayed_field();
  check_process_layout(24, true, fields, 0);
  check_process_layout(22, true, fields, 0);
  check_process_layout(24, false, 0, 0);
  check_process_layout(22, false, 0, 0);
  check_process_layout(26, false, fields, 4);
  puts("UI layout: aligned fields, fixed gutters, wrapping, resize and process space passed");
  return 0;
}
