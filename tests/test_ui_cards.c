#include "nvtop/device_card.h"
#include "nvtop/interface.h"
#include "nvtop/interface_internal_common.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void check_sizing(void) {
  const struct {
    unsigned cols, rows, width;
    bool clocks, too_small;
  } cases[] = {{80, 24, 38, false, false}, {160, 50, 76, false, false},
               {312, 50, 76, false, false}, {320, 50, 76, false, false},
               {80, 24, 92, true, true}, {160, 18, 38, false, false}};
  for (unsigned i = 0; i < ARRAY_SIZE(cases); ++i) {
    struct device_cards *cards = device_cards_create(8);
    nvtop_interface_option options = {.has_gpu_info_bar = cases[i].clocks};
    struct device_card_layout layout =
        device_cards_layout(cards, NULL, &options, cases[i].rows, cases[i].cols, false);
    assert(layout.cols == cases[i].width && layout.rows == 3);
    assert(layout.too_small == cases[i].too_small);
    device_cards_destroy(cards);
  }
}

static void find_card(unsigned id, unsigned *row, unsigned *col) {
  char name[32], line[512];
  snprintf(name, sizeof(name), "NPU %-2u [", id);
  for (int y = 0; y < getmaxy(curscr); ++y) {
    mvwinnstr(curscr, y, 0, line, getmaxx(curscr));
    char *found = strstr(line, name);
    if (found) {
      *row = y;
      *col = found - line;
      return;
    }
  }
  assert(!"device card missing from the real interface");
}

static void toggle_card_options(struct nvtop_interface *interface) {
  interface_key(KEY_F(2), interface);
  assert(!is_escape_for_quit(interface));
  interface_key(KEY_DOWN, interface);  // Device display section.
  interface_key(KEY_RIGHT, interface);
  interface_key(KEY_DOWN, interface);  // Primary clocks.
  interface_key('\n', interface);
  interface_key(KEY_DOWN, interface);  // Static specs and secondary clocks.
  interface_key('\n', interface);
}

static void check_transitions(void) {
  FILE *output = tmpfile();
  assert(output);
  fflush(stdout);
  int saved_stdout = dup(STDOUT_FILENO);
  assert(saved_stdout >= 0 && dup2(fileno(output), STDOUT_FILENO) >= 0);
  assert(setenv("TERM", "xterm-256color", 1) == 0);
  LIST_HEAD(devices);
  struct gpu_info fixtures[8] = {0};
  nvtop_interface_option options = {.use_color = true,
      .process_fields_displayed = process_default_displayed_field(),
      .gpu_specific_opts = calloc(8, sizeof(*options.gpu_specific_opts))};
  assert(options.gpu_specific_opts);
  for (unsigned i = 0; i < 8; ++i) {
    struct gpu_info *device = &fixtures[i];
    device->device_id = i;
    SET_GPUINFO_STATIC(&device->static_info, n_shared_cores, 32);
    SET_GPUINFO_STATIC(&device->static_info, l2cache_size, 64);
    SET_GPUINFO_STATIC(&device->static_info, n_exec_engines, 128);
    SET_GPUINFO_DYNAMIC(&device->dynamic_info, gpu_clock_speed, 1234);
    SET_GPUINFO_DYNAMIC(&device->dynamic_info, mem_clock_speed, 1600);
    SET_GPUINFO_DYNAMIC(&device->dynamic_info, fan_rpm, 4321);
    device->dynamic_info.extra_clock_count = 4;
    for (unsigned j = 0; j < 4; ++j) {
      snprintf(device->dynamic_info.extra_clocks[j].name, EXTRA_CLOCK_NAME_LEN, "CLK%u", j);
      device->dynamic_info.extra_clocks[j].speed = 100 + j;
      device->dynamic_info.extra_clocks[j].secondary = j != 0;
    }
    SET_VALID(gpuinfo_extra_clocks_valid, device->dynamic_info.valid);
    list_add_tail(&device->list, &devices);
    options.gpu_specific_opts[i].linkedGpu = device;
  }
  struct nvtop_interface *interface = initialize_curses(8, 8, 0, options);
  SCREEN *screen = set_term(NULL);
  assert(screen);
  set_term(screen);
  assert(resizeterm(50, 320) == OK);
  update_window_size_to_terminal_size(interface);
  draw_gpu_info_ncurses(8, &devices, interface);
  toggle_card_options(interface);
  draw_gpu_info_ncurses(8, &devices, interface);
  assert(!is_escape_for_quit(interface));
  struct device_card_layout layout =
      device_cards_layout(interface->cards, &devices, &interface->options, 50, 320, true);
  assert(!layout.changed && layout.rows == 3 && layout.cols == 76);
  interface_key(27, interface);
  draw_gpu_info_ncurses(8, &devices, interface);
  assert(is_escape_for_quit(interface));
  layout = device_cards_layout(interface->cards, &devices, &interface->options, 50, 320, true);
  assert(!layout.changed && layout.rows == 6 && layout.cols == 118);
  unsigned row_before[8], col_before[8];
  for (unsigned i = 0; i < 8; ++i) {
    find_card(i, &row_before[i], &col_before[i]);
    memset(fixtures[i].dynamic_info.valid, 0, sizeof(fixtures[i].dynamic_info.valid));
    memset(fixtures[i].static_info.valid, 0, sizeof(fixtures[i].static_info.valid));
  }
  char line[512];
  mvwinnstr(curscr, 3, 0, line, 105);
  assert(strstr(line, "NSHC 32") && strstr(line, "L2CF 64"));
  mvwinnstr(curscr, 5, 0, line, 105);
  assert(strstr(line, "CLK2 102MHz") && strstr(line, "CLK3 103MHz"));
  draw_gpu_info_ncurses(8, &devices, interface);
  layout = device_cards_layout(interface->cards, &devices, &interface->options, 50, 320, true);
  assert(layout.rows == 6 && layout.cols == 118);
  for (unsigned i = 0; i < 8; ++i) {
    unsigned row, col;
    find_card(i, &row, &col);
    assert(row == row_before[i] && col == col_before[i]);
  }
  mvwinnstr(curscr, 0, 0, line, 159);
  assert(strstr(line, "NPU N/A") && strstr(line, "HBM N/A"));
  mvwinnstr(curscr, 3, 0, line, 159);
  assert(!strstr(line, "NSHC") && !strstr(line, "CLK"));
  toggle_card_options(interface);
  draw_gpu_info_ncurses(8, &devices, interface);
  layout = device_cards_layout(interface->cards, &devices, &interface->options, 50, 320, true);
  assert(!layout.changed && layout.rows == 6 && layout.cols == 118);
  // A terminal resize closes setup but must retain its pending option change.
  assert(resizeterm(50, 312) == OK);
  update_window_size_to_terminal_size(interface);
  draw_gpu_info_ncurses(8, &devices, interface);
  assert(is_escape_for_quit(interface));
  layout = device_cards_layout(interface->cards, &devices, &interface->options, 50, 312, true);
  assert(layout.rows == 3 && layout.cols == 76);
  const unsigned widths[] = {160, 320, 80, 320, 30, 80};
  for (unsigned n = 0; n < ARRAY_SIZE(widths); ++n) {
    unsigned width = widths[n], height = width == 320 ? 50 : 24;
    assert(resizeterm(height, width) == OK);
    update_window_size_to_terminal_size(interface);
    draw_gpu_info_ncurses(8, &devices, interface);
    if (width == 30) {
      mvwinnstr(curscr, 0, 0, line, width);
      assert(strstr(line, "enlarge terminal"));
      continue;
    }
    for (unsigned i = 0; i < 8; ++i) {
      unsigned row, col;
      find_card(i, &row, &col);
      if (width == 80) {
        assert(row == i / 2 * 4 && col == i % 2 * 41);
        mvwinnstr(curscr, row, col, line, 39);
        assert(!strstr(line, "PCIe"));
      }
    }
  }
  clean_ncurses(interface);
  delscreen(screen);
  fflush(stdout);
  assert(dup2(saved_stdout, STDOUT_FILENO) >= 0);
  close(saved_stdout);
  fclose(output);
}

int main(void) {
  check_sizing();
  check_transitions();
  puts("UI cards: real interface resize, field loss, option shrink and setup deferral passed");
  return 0;
}
