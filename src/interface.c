/*
 *
 * Copyright (C) 2017-2022 Maxime Schmitt <maxime.schmitt91@gmail.com>
 *
 * This file is part of Nvtop.
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
 *
 */

#include "nvtop/common.h"
#include "nvtop/device_card.h"
#include "nvtop/extract_gpuinfo_common.h"
#include "nvtop/interface.h"
#include "nvtop/interface_common.h"
#include "nvtop/interface_internal_common.h"
#include "nvtop/interface_layout_selection.h"
#include "nvtop/interface_options.h"
#include "nvtop/interface_ring_buffer.h"
#include "nvtop/interface_setup_win.h"
#include "nvtop/pcie_utilization.h"
#include "nvtop/plot.h"
#include "nvtop/time.h"

#include <assert.h>
#include <inttypes.h>
#include <ncurses.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tgmath.h>
#include <unistd.h>

static unsigned int sizeof_process_field[process_field_count] = {
    [process_pid] = 7,       [process_user] = 4,          [process_gpu_id] = 3,   [process_type] = 8,
    [process_gpu_rate] = 4,  [process_enc_rate] = 4,      [process_dec_rate] = 4,
    [process_memory] = 14, // 9 for mem 5 for %
    [process_cpu_usage] = 6, [process_cpu_mem_usage] = 9, [process_command] = 0,
};

static void alloc_process_with_option(struct nvtop_interface *interface, unsigned posX, unsigned posY, unsigned sizeX,
                                      unsigned sizeY) {
  if (sizeY > 0) {
    interface->process.process_win = newwin(sizeY, sizeX, posY, posX);
    interface->process.process_with_option_win =
        newwin(sizeY, sizeX - option_window_size, posY, posX + option_window_size);
  } else {
    interface->process.process_win = NULL;
    interface->process.process_with_option_win = NULL;
  }
  interface->process.selected_row = 0;
  interface->process.selected_pid = -1;
  interface->process.offset_column = 0;
  interface->process.offset = 0;

  interface->process.option_window.option_win = newwin(sizeY, option_window_size, posY, posX);

  interface->process.option_window.state = nvtop_option_state_hidden;
  interface->process.option_window.previous_state = nvtop_option_state_sort_by;
  interface->process.option_window.offset = 0;
  interface->process.option_window.selected_row = 0;
}

static void initialize_gpu_mem_plot(struct plot_window *plot, struct window_position *position,
                                    nvtop_interface_option *options) {
  unsigned rows = position->sizeY;
  unsigned cols = position->sizeX;
  cols -= 5;
  rows -= 2;
  plot->plot_window = newwin(rows, cols, position->posY + 1, position->posX + 4);
  draw_rectangle(plot->win, 3, 0, cols + 2, rows + 2);
  mvwprintw(plot->win, 1 + rows * 3 / 4, 0, " 25");
  mvwprintw(plot->win, 1 + rows / 4, 0, " 75");
  mvwprintw(plot->win, 1 + rows / 2, 0, " 50");
  mvwprintw(plot->win, 1, 0, "100");
  mvwprintw(plot->win, rows, 0, "  0");
  plot->data = calloc(cols, sizeof(*plot->data));
  plot->num_data = cols;

  unsigned column_divisor = 0;
  for (unsigned i = 0; i < plot->num_devices_to_plot; ++i) {
    unsigned dev_id = plot->devices_ids[i];
    plot_info_to_draw to_draw = options->gpu_specific_opts[dev_id].to_draw;
    column_divisor += plot_count_draw_info(to_draw);
  }
  assert(column_divisor > 0);
  char elapsedSeconds[16];
  char *err = "err";
  char *zeroSec = "0s";
  if (options->plot_left_to_right) {
    char *toPrint = zeroSec;
    mvwprintw(plot->win, position->sizeY - 1, 4, "%s", toPrint);

    int retval = snprintf(elapsedSeconds, sizeof(elapsedSeconds), "%ds",
                          options->update_interval * cols / 4 / column_divisor / 1000);
    if (retval >= (int)sizeof(elapsedSeconds))
      toPrint = err;
    else
      toPrint = elapsedSeconds;
    mvwprintw(plot->win, position->sizeY - 1, 4 + cols / 4 - strlen(toPrint) / 2, "%s", toPrint);

    retval = snprintf(elapsedSeconds, sizeof(elapsedSeconds), "%ds",
                      options->update_interval * cols / 2 / column_divisor / 1000);
    if (retval >= (int)sizeof(elapsedSeconds))
      toPrint = err;
    else
      toPrint = elapsedSeconds;
    mvwprintw(plot->win, position->sizeY - 1, 4 + cols / 2 - strlen(toPrint) / 2, "%s", toPrint);

    retval = snprintf(elapsedSeconds, sizeof(elapsedSeconds), "%ds",
                      options->update_interval * cols * 3 / 4 / column_divisor / 1000);
    if (retval >= (int)sizeof(elapsedSeconds))
      toPrint = err;
    else
      toPrint = elapsedSeconds;
    mvwprintw(plot->win, position->sizeY - 1, 4 + cols * 3 / 4 - strlen(toPrint) / 2, "%s", toPrint);

    retval = snprintf(elapsedSeconds, sizeof(elapsedSeconds), "%ds",
                      options->update_interval * cols / column_divisor / 1000);
    if (retval >= (int)sizeof(elapsedSeconds))
      toPrint = err;
    else
      toPrint = elapsedSeconds;
    mvwprintw(plot->win, position->sizeY - 1, 4 + cols - strlen(toPrint), "%s", toPrint);
  } else {
    char *toPrint;
    int retval = snprintf(elapsedSeconds, sizeof(elapsedSeconds), "%ds",
                          options->update_interval * cols / column_divisor / 1000);
    if (retval >= (int)sizeof(elapsedSeconds))
      toPrint = err;
    else
      toPrint = elapsedSeconds;
    mvwprintw(plot->win, position->sizeY - 1, 4, "%s", toPrint);

    retval = snprintf(elapsedSeconds, sizeof(elapsedSeconds), "%ds",
                      options->update_interval * cols * 3 / 4 / column_divisor / 1000);
    if (retval >= (int)sizeof(elapsedSeconds))
      toPrint = err;
    else
      toPrint = elapsedSeconds;
    mvwprintw(plot->win, position->sizeY - 1, 4 + cols / 4 - strlen(toPrint) / 2, "%s", toPrint);

    retval = snprintf(elapsedSeconds, sizeof(elapsedSeconds), "%ds",
                      options->update_interval * cols / 2 / column_divisor / 1000);
    if (retval >= (int)sizeof(elapsedSeconds))
      toPrint = err;
    else
      toPrint = elapsedSeconds;
    mvwprintw(plot->win, position->sizeY - 1, 4 + cols / 2 - strlen(toPrint) / 2, "%s", toPrint);

    retval = snprintf(elapsedSeconds, sizeof(elapsedSeconds), "%ds",
                      options->update_interval * cols / 4 / column_divisor / 1000);
    if (retval >= (int)sizeof(elapsedSeconds))
      toPrint = err;
    else
      toPrint = elapsedSeconds;
    mvwprintw(plot->win, position->sizeY - 1, 4 + cols * 3 / 4 - strlen(toPrint) / 2, "%s", toPrint);

    toPrint = zeroSec;
    mvwprintw(plot->win, position->sizeY - 1, 4 + cols - strlen(toPrint), "%s", toPrint);
  }
  wnoutrefresh(plot->win);
}

static void alloc_plot_window(unsigned devices_count, struct window_position *plot_positions,
                              unsigned map_device_to_plot[devices_count], struct nvtop_interface *interface) {
  if (!interface->num_plots) {
    interface->plots = NULL;
    return;
  }
  interface->plots = malloc(interface->num_plots * sizeof(*interface->plots));
  for (size_t i = 0; i < interface->num_plots; ++i) {
    interface->plots[i].num_devices_to_plot = 0;
    for (unsigned dev_id = 0; dev_id < devices_count; ++dev_id) {
      if (map_device_to_plot[dev_id] == i) {
        interface->plots[i].devices_ids[interface->plots[i].num_devices_to_plot] = dev_id;
        interface->plots[i].num_devices_to_plot++;
      }
    }
    interface->plots[i].win =
        newwin(plot_positions[i].sizeY, plot_positions[i].sizeX, plot_positions[i].posY, plot_positions[i].posX);
    initialize_gpu_mem_plot(&interface->plots[i], &plot_positions[i], &interface->options);
  }
}

static pid_t nvtop_pid;

static void initialize_all_windows(struct nvtop_interface *dwin) {
  int rows, cols;
  getmaxyx(stdscr, rows, cols);

  unsigned int devices_count = dwin->monitored_dev_count;
  struct device_card_layout card_layout =
      device_cards_layout(dwin->cards, NULL, &dwin->options, rows, cols, false);
  dwin->layout_too_small = card_layout.too_small;
  if (dwin->layout_too_small) {
    dwin->num_plots = 0;
    dwin->setup_win.visible = false;
    dwin->process.option_window.state = nvtop_option_state_hidden;
    return;
  }

  struct window_position device_positions[devices_count];
  unsigned map_device_to_plot[devices_count];
  struct window_position process_position;
  struct window_position plot_positions[MAX_CHARTS];
  struct window_position setup_position;

  compute_sizes_from_layout(devices_count, card_layout.rows, card_layout.cols, rows - 1, cols,
                            dwin->options.gpu_specific_opts, dwin->options.process_fields_displayed, device_positions,
                            &dwin->num_plots, plot_positions, map_device_to_plot, &process_position, &setup_position,
                            dwin->options.hide_processes_list);

  alloc_plot_window(devices_count, plot_positions, map_device_to_plot, dwin);

  device_cards_allocate(dwin->cards, device_positions);

  alloc_process_with_option(dwin, process_position.posX, process_position.posY, process_position.sizeX,
                            process_position.sizeY);

  dwin->shortcut_window = newwin(1, cols, rows - 1, 0);

  // Setup needs enough height for all metrics and device choices on compact terminals.
  setup_position.posY = 0;
  setup_position.sizeY = rows - 1;
  alloc_setup_window(&setup_position, &dwin->setup_win);
  nvtop_pid = getpid();
}

static void delete_all_windows(struct nvtop_interface *dwin) {
  if (dwin->layout_too_small)
    return;
  device_cards_release_windows(dwin->cards);
  delwin(dwin->process.process_win);
  delwin(dwin->process.process_with_option_win);
  dwin->process.process_win = NULL;
  dwin->process.process_with_option_win = NULL;
  delwin(dwin->shortcut_window);
  delwin(dwin->process.option_window.option_win);
  for (size_t i = 0; i < dwin->num_plots; ++i) {
    delwin(dwin->plots[i].win);
    delwin(dwin->plots[i].plot_window);
    free(dwin->plots[i].data);
  }
  free_setup_window(&dwin->setup_win);
  free(dwin->plots);
}

static short brand_teal = COLOR_CYAN;
static short label_gray = COLOR_WHITE;
static short plot_blue = COLOR_BLUE;

static void initialize_colors(const unsigned char plot_color_idx[MAX_LINES_PER_PLOT]) {
  if (!has_colors())
    return;
  start_color();
  // Ascend teal/gray and a soft blue #7AA2F7, with native terminal fallbacks.
  if (COLORS >= 19 && can_change_color()) {
    brand_teal = 16;
    label_gray = 17;
    plot_blue = 18;
    init_color(brand_teal, 188, 710, 773);
    init_color(label_gray, 855, 855, 851);
    init_color(plot_blue, 478, 635, 969);
  } else if (COLORS >= 256) {
    brand_teal = 38;
    label_gray = 253;
    plot_blue = 111;
  }
  init_pair(cyan_color, label_gray, COLOR_BLACK);
  init_pair(warning_color, COLOR_YELLOW, COLOR_BLACK);
  init_pair(green_color, COLOR_GREEN, COLOR_BLACK);
  init_pair(yellow_color, COLOR_YELLOW, COLOR_BLACK);
  init_pair(blue_color, plot_blue, COLOR_BLACK);
  init_pair(magenta_color, COLOR_MAGENTA, COLOR_BLACK);
  init_pair(selected_color, COLOR_BLACK, brand_teal);
  static const short gpu_plot_pairs[MAX_LINES_PER_PLOT] = {
      gpu_util_plot_color, gpu_mem_plot_color, gpu_plot_color_3, gpu_plot_color_4};
  const short plot_colors[] = {brand_teal, COLOR_CYAN, COLOR_GREEN, COLOR_YELLOW,
                               plot_blue, COLOR_MAGENTA, COLOR_WHITE};
  for (unsigned i = 0; i < MAX_LINES_PER_PLOT; ++i)
    init_pair(gpu_plot_pairs[i], plot_colors[plot_color_idx[i] % ARRAY_SIZE(plot_colors)], COLOR_BLACK);
}

struct nvtop_interface *initialize_curses(unsigned total_devices, unsigned devices_count, unsigned largest_device_name,
                                          nvtop_interface_option options) {
  struct nvtop_interface *interface = calloc(1, sizeof(*interface));
  interface->options = options;
  interface->cards = device_cards_create(devices_count);
  interface->total_dev_count = total_devices;
  interface->monitored_dev_count = devices_count;
  (void)largest_device_name;
  initscr();
  refresh();
  if (interface->options.use_color && has_colors() == TRUE) {
    initialize_colors(options.gpu_plot_color_idx);
  }
  cbreak();
  noecho();
  keypad(stdscr, TRUE);
  curs_set(0);

  interface_alloc_ring_buffer(devices_count, 4, 10 * 60 * 10 + 1, &interface->saved_data_ring);
  initialize_all_windows(interface);
  return interface;
}

void apply_plot_colors(const unsigned char plot_color_idx[MAX_LINES_PER_PLOT]) {
  initialize_colors(plot_color_idx);
}

void clean_ncurses(struct nvtop_interface *interface) {
  endwin();
  delete_all_windows(interface);
  free(interface->options.gpu_specific_opts);
  free(interface->options.config_file_location);
  device_cards_destroy(interface->cards);
  interface_free_ring_buffer(&interface->saved_data_ring);
  free(interface);
}

static const char *memory_prefix[] = {" B", "Ki", "Mi", "Gi", "Ti", "Pi"};

static void format_bytes(char *buffer, size_t size, uint64_t bytes) {
  double value = bytes;
  size_t unit = 0;
  while (unit < sizeof(memory_prefix) / sizeof(*memory_prefix) - 1 && value >= 1000.) {
    value /= 1024.;
    ++unit;
  }
  const char *suffix = unit ? "B" : "";

  if (unit == 0 || value >= 100.)
    snprintf(buffer, size, "%.0f%s%s", value, memory_prefix[unit], suffix);
  else if (value >= 10.)
    snprintf(buffer, size, "%.1f%s%s", value, memory_prefix[unit], suffix);
  else
    snprintf(buffer, size, "%.2f%s%s", value, memory_prefix[unit], suffix);
}

static struct gpu_info *get_device_by_id(struct list_head *devices, unsigned id) {
  struct gpu_info *device;
  unsigned i = 0;
  list_for_each_entry(device, devices, list) {
    if (i++ == id)
      return device;
  }
  return NULL;
}

typedef struct {
  unsigned processes_count;
  struct gpuid_and_process {
    unsigned gpu_id;
    struct gpu_process *process;
  } *processes;
} all_processes;

static all_processes all_processes_array(struct list_head *devices) {
  unsigned total_processes_count = 0;
  struct gpu_info *device;

  list_for_each_entry(device, devices, list) { total_processes_count += device->processes_count; }

  all_processes merged_devices_processes;
  merged_devices_processes.processes_count = total_processes_count;
  if (total_processes_count) {
    merged_devices_processes.processes = malloc(total_processes_count * sizeof(*merged_devices_processes.processes));
    if (!merged_devices_processes.processes) {
      perror("Cannot allocate memory: ");
      exit(EXIT_FAILURE);
    }
  } else {
    merged_devices_processes.processes = NULL;
  }

  size_t offset = 0;
  list_for_each_entry(device, devices, list) {
    for (unsigned int j = 0; j < device->processes_count; ++j) {
      merged_devices_processes.processes[offset].gpu_id = device->device_id;
      merged_devices_processes.processes[offset++].process = &device->processes[j];
    }

  }

  return merged_devices_processes;
}

static int compare_pid_desc(const void *pp1, const void *pp2) {
  const struct gpuid_and_process *p1 = (const struct gpuid_and_process *)pp1;
  const struct gpuid_and_process *p2 = (const struct gpuid_and_process *)pp2;
  return p1->process->pid >= p2->process->pid ? -1 : 1;
}

static int compare_pid_asc(const void *pp1, const void *pp2) { return compare_pid_desc(pp2, pp1); }

static int compare_username_desc(const void *pp1, const void *pp2) {
  const struct gpuid_and_process *p1 = (const struct gpuid_and_process *)pp1;
  const struct gpuid_and_process *p2 = (const struct gpuid_and_process *)pp2;
  if (GPUINFO_PROCESS_FIELD_VALID(p1->process, user_name) && GPUINFO_PROCESS_FIELD_VALID(p2->process, user_name))
    return -strcmp(p1->process->user_name, p2->process->user_name);
  else
    return 0;
}

static int compare_username_asc(const void *pp1, const void *pp2) { return compare_username_desc(pp2, pp1); }

static int compare_process_name_desc(const void *pp1, const void *pp2) {
  const struct gpuid_and_process *p1 = (const struct gpuid_and_process *)pp1;
  const struct gpuid_and_process *p2 = (const struct gpuid_and_process *)pp2;
  if (GPUINFO_PROCESS_FIELD_VALID(p1->process, cmdline) && GPUINFO_PROCESS_FIELD_VALID(p2->process, cmdline))
    return -strcmp(p1->process->cmdline, p2->process->cmdline);
  else
    return 0;
}

static int compare_process_name_asc(const void *pp1, const void *pp2) { return compare_process_name_desc(pp2, pp1); }

static int compare_mem_usage_desc(const void *pp1, const void *pp2) {
  const struct gpuid_and_process *p1 = (const struct gpuid_and_process *)pp1;
  const struct gpuid_and_process *p2 = (const struct gpuid_and_process *)pp2;
  if (GPUINFO_PROCESS_FIELD_VALID(p1->process, gpu_memory_usage) &&
      GPUINFO_PROCESS_FIELD_VALID(p2->process, gpu_memory_usage))
    return p1->process->gpu_memory_usage >= p2->process->gpu_memory_usage ? -1 : 1;
  else
    return 0;
}

static int compare_mem_usage_asc(const void *pp1, const void *pp2) { return compare_mem_usage_desc(pp2, pp1); }

static int compare_cpu_usage_desc(const void *pp1, const void *pp2) {
  const struct gpuid_and_process *p1 = (const struct gpuid_and_process *)pp1;
  const struct gpuid_and_process *p2 = (const struct gpuid_and_process *)pp2;
  if (GPUINFO_PROCESS_FIELD_VALID(p1->process, cpu_usage) && GPUINFO_PROCESS_FIELD_VALID(p2->process, cpu_usage))
    return p1->process->cpu_usage >= p2->process->cpu_usage ? -1 : 1;
  else
    return 0;
}

static int compare_cpu_usage_asc(const void *pp1, const void *pp2) { return compare_cpu_usage_desc(pp2, pp1); }

static int compare_cpu_mem_usage_desc(const void *pp1, const void *pp2) {
  const struct gpuid_and_process *p1 = (const struct gpuid_and_process *)pp1;
  const struct gpuid_and_process *p2 = (const struct gpuid_and_process *)pp2;
  if (GPUINFO_PROCESS_FIELD_VALID(p1->process, cpu_memory_res) &&
      GPUINFO_PROCESS_FIELD_VALID(p2->process, cpu_memory_res))
    return p1->process->cpu_memory_res >= p2->process->cpu_memory_res ? -1 : 1;
  else
    return 0;
}

static int compare_cpu_mem_usage_asc(const void *pp1, const void *pp2) { return compare_cpu_mem_usage_desc(pp2, pp1); }

static int compare_gpu_desc(const void *pp1, const void *pp2) {
  const struct gpuid_and_process *p1 = (const struct gpuid_and_process *)pp1;
  const struct gpuid_and_process *p2 = (const struct gpuid_and_process *)pp2;
  return p1->gpu_id >= p2->gpu_id ? -1 : 1;
}

static int compare_gpu_asc(const void *pp1, const void *pp2) { return -compare_gpu_desc(pp1, pp2); }

static int compare_process_type_desc(const void *pp1, const void *pp2) {
  const struct gpuid_and_process *p1 = (const struct gpuid_and_process *)pp1;
  const struct gpuid_and_process *p2 = (const struct gpuid_and_process *)pp2;
  return (p1->process->type == gpu_process_graphical) != (p2->process->type == gpu_process_graphical);
}

static int compare_process_type_asc(const void *pp1, const void *pp2) { return -compare_process_name_desc(pp1, pp2); }

static int compare_process_gpu_rate_desc(const void *pp1, const void *pp2) {
  const struct gpuid_and_process *p1 = (const struct gpuid_and_process *)pp1;
  const struct gpuid_and_process *p2 = (const struct gpuid_and_process *)pp2;
  if (GPUINFO_PROCESS_FIELD_VALID(p1->process, gpu_usage) && GPUINFO_PROCESS_FIELD_VALID(p2->process, gpu_usage)) {
    return p1->process->gpu_usage > p2->process->gpu_usage ? -1 : 1;
  } else {
    if (GPUINFO_PROCESS_FIELD_VALID(p1->process, gpu_usage)) {
      return p1->process->gpu_usage > 0 ? -1 : 0;
    } else if (GPUINFO_PROCESS_FIELD_VALID(p2->process, gpu_usage)) {
      return p2->process->gpu_usage > 0 ? 1 : 0;
    } else {
      return 0;
    }
  }
}

static int compare_process_gpu_rate_asc(const void *pp1, const void *pp2) {
  return -compare_process_gpu_rate_desc(pp1, pp2);
}

static int compare_process_enc_rate_desc(const void *pp1, const void *pp2) {
  const struct gpuid_and_process *p1 = (const struct gpuid_and_process *)pp1;
  const struct gpuid_and_process *p2 = (const struct gpuid_and_process *)pp2;
  if (GPUINFO_PROCESS_FIELD_VALID(p1->process, encode_usage) &&
      GPUINFO_PROCESS_FIELD_VALID(p2->process, encode_usage)) {
    return p1->process->encode_usage >= p2->process->encode_usage ? -1 : 1;
  } else {
    if (GPUINFO_PROCESS_FIELD_VALID(p1->process, encode_usage)) {
      return p1->process->encode_usage > 0 ? -1 : 0;
    } else if (GPUINFO_PROCESS_FIELD_VALID(p2->process, encode_usage)) {
      return p2->process->encode_usage > 0 ? 1 : 0;
    } else {
      return 0;
    }
  }
}

static int compare_process_enc_rate_asc(const void *pp1, const void *pp2) {
  return -compare_process_enc_rate_desc(pp1, pp2);
}

static int compare_process_dec_rate_desc(const void *pp1, const void *pp2) {
  const struct gpuid_and_process *p1 = (const struct gpuid_and_process *)pp1;
  const struct gpuid_and_process *p2 = (const struct gpuid_and_process *)pp2;
  if (GPUINFO_PROCESS_FIELD_VALID(p1->process, decode_usage) &&
      GPUINFO_PROCESS_FIELD_VALID(p2->process, decode_usage)) {
    return p1->process->decode_usage >= p2->process->decode_usage ? -1 : 1;
  } else {
    if (GPUINFO_PROCESS_FIELD_VALID(p1->process, decode_usage)) {
      return p1->process->decode_usage > 0 ? -1 : 0;
    } else if (GPUINFO_PROCESS_FIELD_VALID(p2->process, decode_usage)) {
      return p2->process->decode_usage > 0 ? 1 : 0;
    } else {
      return 0;
    }
  }
}
static int compare_process_dec_rate_asc(const void *pp1, const void *pp2) {
  return -compare_process_dec_rate_desc(pp1, pp2);
}

static void sort_process(all_processes all_procs, enum process_field criterion, bool asc_sort) {
  if (all_procs.processes_count == 0 || !all_procs.processes)
    return;
  int (*sort_fun)(const void *, const void *);
  switch (criterion) {
  case process_pid:
    if (asc_sort)
      sort_fun = compare_pid_asc;
    else
      sort_fun = compare_pid_desc;
    break;
  case process_user:
    if (asc_sort)
      sort_fun = compare_username_asc;
    else
      sort_fun = compare_username_desc;
    break;
  case process_gpu_id:
    if (asc_sort)
      sort_fun = compare_gpu_asc;
    else
      sort_fun = compare_gpu_desc;
    break;
  case process_type:
    if (asc_sort)
      sort_fun = compare_process_type_asc;
    else
      sort_fun = compare_process_type_desc;
    break;
  case process_memory:
    if (asc_sort)
      sort_fun = compare_mem_usage_asc;
    else
      sort_fun = compare_mem_usage_desc;
    break;
  case process_command:
    if (asc_sort)
      sort_fun = compare_process_name_asc;
    else
      sort_fun = compare_process_name_desc;
    break;
  case process_cpu_usage:
    if (asc_sort)
      sort_fun = compare_cpu_usage_asc;
    else
      sort_fun = compare_cpu_usage_desc;
    break;
  case process_cpu_mem_usage:
    if (asc_sort)
      sort_fun = compare_cpu_mem_usage_asc;
    else
      sort_fun = compare_cpu_mem_usage_desc;
    break;
  case process_gpu_rate:
    if (asc_sort)
      sort_fun = compare_process_gpu_rate_asc;
    else
      sort_fun = compare_process_gpu_rate_desc;
    break;
  case process_enc_rate:
    if (asc_sort)
      sort_fun = compare_process_enc_rate_asc;
    else
      sort_fun = compare_process_enc_rate_desc;
    break;
  case process_dec_rate:
    if (asc_sort)
      sort_fun = compare_process_dec_rate_asc;
    else
      sort_fun = compare_process_dec_rate_desc;
    break;
  case process_field_count:
  default:
    return;
  }
  qsort(all_procs.processes, all_procs.processes_count, sizeof(*all_procs.processes), sort_fun);
}

static void filter_out_nvtop_pid(all_processes *all_procs, struct nvtop_interface *interface) {
  if (interface->options.filter_nvtop_pid) {
    for (unsigned procId = 0; procId < all_procs->processes_count; ++procId) {
      if (all_procs->processes[procId].process->pid == nvtop_pid) {
        memmove(&all_procs->processes[procId], &all_procs->processes[procId + 1],
                (all_procs->processes_count - procId - 1) * sizeof(*all_procs->processes));
        all_procs->processes_count = all_procs->processes_count - 1;
        break;
      }
    }
  }
}

static const char *columnName[process_field_count] = {
    "PID", "USER", "DEV", "TYPE", "NPU", "ENC", "DEC", "HBM MEM", "CPU", "HOST MEM", "Command",
};

static void update_selected_offset_with_window_size(unsigned int *selected_row, unsigned int *offset,
                                                    unsigned int row_available_to_draw, unsigned int num_to_draw) {

  if (!num_to_draw)
    return;

  if (*selected_row > num_to_draw - 1)
    *selected_row = num_to_draw - 1;

  if (*offset > *selected_row)
    *offset = *selected_row;

  if (*offset + row_available_to_draw - 1 < *selected_row)
    *offset = *selected_row - row_available_to_draw + 1;

  while (row_available_to_draw > num_to_draw - *offset && *offset != 0)
    *offset -= 1;
}

#define process_buffer_line_size 8192
static char process_print_buffer[process_buffer_line_size];

static void print_processes_on_screen(all_processes all_procs, struct process_window *process,
                                      enum process_field sort_criterion, process_field_displayed fields_to_display,
                                      bool dynamic_memory_units) {
  WINDOW *win = process->option_window.state == nvtop_option_state_hidden ? process->process_win
                                                                          : process->process_with_option_win;
  struct gpuid_and_process *processes = all_procs.processes;

  unsigned int rows, cols;
  getmaxyx(win, rows, cols);
  rows -= 1;

  update_selected_offset_with_window_size(&process->selected_row, &process->offset, rows, all_procs.processes_count);
  if (process->offset_column + cols >= process_buffer_line_size)
    process->offset_column = process_buffer_line_size - cols - 1;

  size_t special_row = process->selected_row;

  char pid_str[sizeof_process_field[process_pid] + 1];
  char guid_str[sizeof_process_field[process_gpu_id] + 1];
  char memory[sizeof_process_field[process_memory] + 1];
  char cpu_percent[sizeof_process_field[process_cpu_usage] + 1];
  char cpu_mem[sizeof_process_field[process_cpu_mem_usage] + 1];

  unsigned int start_at_process = process->offset;
  unsigned int end_at_process = start_at_process + rows;

  int printed = 0;
  int column_sort_start = 0, column_sort_end = sizeof_process_field[0];
  memset(process_print_buffer, 0, sizeof(process_print_buffer));
  for (enum process_field i = process_pid; i < process_field_count; ++i) {
    if (i == sort_criterion) {
      column_sort_start = printed;
      column_sort_end =
          i == process_command ? process_buffer_line_size - 4 : column_sort_start + sizeof_process_field[i];
    }
    if (process_is_field_displayed(i, fields_to_display))
      printed += snprintf(&process_print_buffer[printed], process_buffer_line_size - printed, "%*s ",
                          sizeof_process_field[i], columnName[i]);
  }

  mvwprintw(win, 0, 0, "%.*s", cols, &process_print_buffer[process->offset_column]);
  wclrtoeol(win);
  mvwchgat(win, 0, 0, -1, A_NORMAL, selected_color, NULL);
  set_attribute_between(win, 0, column_sort_start - (int)process->offset_column,
                        column_sort_end - (int)process->offset_column, A_NORMAL, selected_color);

  int start_col_process_type = 0;
  for (enum process_field i = process_pid; i < process_type; ++i) {
    if (process_is_field_displayed(i, fields_to_display))
      start_col_process_type += sizeof_process_field[i] + 1;
  }
  int end_col_process_type = start_col_process_type + sizeof_process_field[process_type];

  static unsigned printed_last_call = 0;
  unsigned last_line_printed = 0;
  for (unsigned int i = start_at_process; i < end_at_process && i < all_procs.processes_count; ++i) {
    memset(process_print_buffer, 0, sizeof(process_print_buffer));

    printed = 0;
    if (process_is_field_displayed(process_pid, fields_to_display)) {
      size_t size =
          snprintf(pid_str, sizeof_process_field[process_pid] + 1, "%" PRIdMAX, (intmax_t)processes[i].process->pid);
      if (size == sizeof_process_field[process_pid] + 1)
        pid_str[sizeof_process_field[process_pid]] = '\0';
      printed += snprintf(&process_print_buffer[printed], process_buffer_line_size, "%*s ",
                          sizeof_process_field[process_pid], pid_str);
    }

    if (process_is_field_displayed(process_user, fields_to_display)) {
      const char *username;
      if (GPUINFO_PROCESS_FIELD_VALID(processes[i].process, user_name)) {
        username = processes[i].process->user_name;
      } else {
        username = "N/A";
      }

      printed += snprintf(&process_print_buffer[printed], process_buffer_line_size - printed, "%*s ",
                          sizeof_process_field[process_user], username);
    }

    if (process_is_field_displayed(process_gpu_id, fields_to_display)) {
      size_t size = snprintf(guid_str, sizeof_process_field[process_gpu_id] + 1, "%u", processes[i].gpu_id);
      if (size >= sizeof_process_field[process_gpu_id] + 1)
        pid_str[sizeof_process_field[process_gpu_id]] = '\0';
      printed += snprintf(&process_print_buffer[printed], process_buffer_line_size - printed, "%*s ",
                          sizeof_process_field[process_gpu_id], guid_str);
    }

    if (process_is_field_displayed(process_type, fields_to_display)) {
      if (processes[i].process->type == gpu_process_graphical_compute) {
        printed += snprintf(&process_print_buffer[printed], process_buffer_line_size - printed, "%*s ",
                            sizeof_process_field[process_type], "Both G+C");
      } else if (processes[i].process->type == gpu_process_graphical) {
        printed += snprintf(&process_print_buffer[printed], process_buffer_line_size - printed, "%*s ",
                            sizeof_process_field[process_type], "Graphic");
      } else {
        printed += snprintf(&process_print_buffer[printed], process_buffer_line_size - printed, "%*s ",
                            sizeof_process_field[process_type], "Compute");
      }
    }

    if (process_is_field_displayed(process_gpu_rate, fields_to_display)) {
      unsigned gpu_usage = 0;
      if (GPUINFO_PROCESS_FIELD_VALID(processes[i].process, gpu_usage)) {
        gpu_usage = processes[i].process->gpu_usage;
        printed += snprintf(&process_print_buffer[printed], process_buffer_line_size - printed, "%3u%% ", gpu_usage);
      } else {
        printed += snprintf(&process_print_buffer[printed], process_buffer_line_size - printed, "N/A  ");
      }
    }

    if (process_is_field_displayed(process_enc_rate, fields_to_display)) {
      unsigned encoder_rate = 0;
      if (GPUINFO_PROCESS_FIELD_VALID(processes[i].process, encode_usage)) {
        encoder_rate = processes[i].process->encode_usage;
        printed += snprintf(&process_print_buffer[printed], process_buffer_line_size - printed, "%3u%% ", encoder_rate);
      } else {
        printed += snprintf(&process_print_buffer[printed], process_buffer_line_size - printed, "N/A  ");
      }
    }

    if (process_is_field_displayed(process_dec_rate, fields_to_display)) {
      unsigned decode_rate = 0;
      if (GPUINFO_PROCESS_FIELD_VALID(processes[i].process, decode_usage)) {
        decode_rate = processes[i].process->decode_usage;
        printed += snprintf(&process_print_buffer[printed], process_buffer_line_size - printed, "%3u%% ", decode_rate);
      } else {
        printed += snprintf(&process_print_buffer[printed], process_buffer_line_size - printed, "N/A  ");
      }
    }

    if (process_is_field_displayed(process_memory, fields_to_display)) {
      if (GPUINFO_PROCESS_FIELD_VALID(processes[i].process, gpu_memory_usage)) {
        if (dynamic_memory_units) {
          char formatted_memory[sizeof(memory)];
          format_bytes(formatted_memory, sizeof(formatted_memory), processes[i].process->gpu_memory_usage);
          if (GPUINFO_PROCESS_FIELD_VALID(processes[i].process, gpu_memory_percentage)) {
            snprintf(memory, sizeof(memory), "%s %3u%%", formatted_memory, processes[i].process->gpu_memory_percentage);
          } else {
            snprintf(memory, sizeof(memory), "%s", formatted_memory);
          }
        } else if (GPUINFO_PROCESS_FIELD_VALID(processes[i].process, gpu_memory_percentage)) {
          snprintf(memory, 9 + 1, "%6uMiB", (unsigned)(processes[i].process->gpu_memory_usage / 1048576));
          snprintf(memory + 9, sizeof_process_field[process_memory] - 9 + 1, " %3u%%",
                   processes[i].process->gpu_memory_percentage);
        } else {
          snprintf(memory, sizeof_process_field[process_memory], "%6uMiB",
                   (unsigned)(processes[i].process->gpu_memory_usage / 1048576));
        }
      } else {
        snprintf(memory, sizeof_process_field[process_memory], "%s", "N/A");
      }
      printed += snprintf(&process_print_buffer[printed], process_buffer_line_size - printed, "%*s ",
                          sizeof_process_field[process_memory], memory);
    }

    if (process_is_field_displayed(process_cpu_usage, fields_to_display)) {
      if (GPUINFO_PROCESS_FIELD_VALID(processes[i].process, cpu_usage))
        snprintf(cpu_percent, sizeof_process_field[process_cpu_usage] + 1, "%u%%", processes[i].process->cpu_usage);
      else
        snprintf(cpu_percent, sizeof_process_field[process_cpu_usage] + 1, "   N/A");
      printed += snprintf(&process_print_buffer[printed], process_buffer_line_size - printed, "%*s ",
                          sizeof_process_field[process_cpu_usage], cpu_percent);
    }

    if (process_is_field_displayed(process_cpu_mem_usage, fields_to_display)) {
      if (GPUINFO_PROCESS_FIELD_VALID(processes[i].process, cpu_memory_res)) {
        if (dynamic_memory_units)
          format_bytes(cpu_mem, sizeof(cpu_mem), processes[i].process->cpu_memory_res);
        else
          snprintf(cpu_mem, sizeof_process_field[process_cpu_mem_usage] + 1, "%zuMiB",
                   processes[i].process->cpu_memory_res / 1048576);
      } else {
        snprintf(cpu_mem, sizeof_process_field[process_cpu_mem_usage] + 1, "N/A");
      }
      printed += snprintf(&process_print_buffer[printed], process_buffer_line_size - printed, "%*s ",
                          sizeof_process_field[process_cpu_mem_usage], cpu_mem);
    }

    if (process_is_field_displayed(process_command, fields_to_display)) {
      if (GPUINFO_PROCESS_FIELD_VALID(processes[i].process, cmdline))
        printed += snprintf(&process_print_buffer[printed], process_buffer_line_size - printed, "%.*s",
                            process_buffer_line_size - printed, processes[i].process->cmdline);
    }

    unsigned int write_at = i - start_at_process + 1;
    mvwprintw(win, write_at, 0, "%.*s", cols, &process_print_buffer[process->offset_column]);
    unsigned row, col;
    getyx(win, row, col);
    (void)col;
    if (row == write_at)
      wclrtoeol(win);
    last_line_printed = write_at;
    if (i == special_row) {
      mvwchgat(win, write_at, 0, -1, A_NORMAL, selected_color, NULL);
    } else {
      if (process_is_field_displayed(process_type, fields_to_display)) {
        if (processes[i].process->type == gpu_process_graphical_compute) {
          set_attribute_between(win, write_at, start_col_process_type - (int)process->offset_column,
                                start_col_process_type - (int)process->offset_column + 4, 0, cyan_color);
          set_attribute_between(win, write_at, end_col_process_type - (int)process->offset_column - 3,
                                end_col_process_type - (int)process->offset_column - 2, 0, yellow_color);
          set_attribute_between(win, write_at, end_col_process_type - (int)process->offset_column - 1,
                                end_col_process_type - (int)process->offset_column, 0, magenta_color);
        } else if (processes[i].process->type == gpu_process_graphical) {
          set_attribute_between(win, write_at, start_col_process_type - (int)process->offset_column,
                                end_col_process_type - (int)process->offset_column, 0, yellow_color);
        } else {
          set_attribute_between(win, write_at, start_col_process_type - (int)process->offset_column,
                                end_col_process_type - (int)process->offset_column, 0, magenta_color);
        }
      }
    }
  }
  if (printed_last_call > last_line_printed) {
    for (unsigned i = last_line_printed + 1; i <= rows && i <= printed_last_call; ++i) {
      wmove(win, i, 0);
      wclrtoeol(win);
    }
  }
  printed_last_call = last_line_printed;
  wnoutrefresh(win);
}

static void update_process_option_win(struct nvtop_interface *interface);

static void draw_processes(struct list_head *devices, struct nvtop_interface *interface) {
  if (interface->options.hide_processes_list)
    return;

  if (interface->process.process_win == NULL)
    return;

  if (interface->process.option_window.state != interface->process.option_window.previous_state) {
    werase(interface->process.option_window.option_win);
    wclear(interface->process.process_win);
    wclear(interface->process.process_with_option_win);
    wnoutrefresh(interface->process.option_window.option_win);
  }
  if (interface->process.option_window.state != nvtop_option_state_hidden)
    update_process_option_win(interface);

  all_processes all_procs = all_processes_array(devices);
  filter_out_nvtop_pid(&all_procs, interface);
  sort_process(all_procs, interface->options.sort_processes_by, !interface->options.sort_descending_order);

  if (all_procs.processes_count > 0) {
    if (interface->process.selected_row >= all_procs.processes_count)
      interface->process.selected_row = all_procs.processes_count - 1;
    interface->process.selected_pid = all_procs.processes[interface->process.selected_row].process->pid;
  } else {
    interface->process.selected_row = 0;
    interface->process.selected_pid = -1;
  }

  unsigned largest_username = 4;
  for (unsigned i = 0; i < all_procs.processes_count; ++i) {
    if (GPUINFO_PROCESS_FIELD_VALID(all_procs.processes[i].process, user_name)) {
      unsigned length = strlen(all_procs.processes[i].process->user_name);
      if (length > largest_username)
        largest_username = length;
    }
  }
  sizeof_process_field[process_user] = largest_username;

  print_processes_on_screen(all_procs, &interface->process, interface->options.sort_processes_by,
                            interface->options.process_fields_displayed, interface->options.dynamic_memory_units);
  free(all_procs.processes);
}

static void draw_sort_option(struct nvtop_interface *interface) {
  WINDOW *win = interface->process.option_window.option_win;
  wattr_set(win, A_NORMAL, selected_color, NULL);
  mvwprintw(win, 0, 0, "Sort by     ");
  wstandend(win);
  wprintw(win, " ");
  int rows, cols;
  if (interface->process.option_window.offset == 0) {
    if (interface->process.option_window.selected_row == 0) {
      wattr_set(win, A_NORMAL, selected_color, NULL);
    }
    wprintw(win, "Cancel");
    getyx(win, rows, cols);
    for (unsigned int j = cols; j < option_window_size; ++j)
      wprintw(win, " ");
    if (interface->process.option_window.selected_row == 0) {
      wstandend(win);
      mvwprintw(win, rows, option_window_size - 1, " ");
    }
  }
  getmaxyx(win, rows, cols);

  size_t start_at_option = interface->process.option_window.offset == 0 ? interface->process.option_window.offset
                                                                        : interface->process.option_window.offset - 1;
  size_t end_at_option =
      interface->process.option_window.offset == 0 ? start_at_option + rows - 2 : start_at_option + rows - 1;

  unsigned option_index = 0;
  for (enum process_field field = process_pid; field < process_field_count; ++field) {
    if (process_is_field_displayed(field, interface->options.process_fields_displayed)) {
      if (option_index >= start_at_option && option_index < end_at_option) {
        if (option_index + 1 == interface->process.option_window.selected_row) {
          wattr_set(win, A_NORMAL, selected_color, NULL);
        }
        wprintw(win, "%s", columnName[field]);
        getyx(win, rows, cols);
        for (unsigned int j = cols; j < option_window_size; ++j)
          wprintw(win, " ");

        if (option_index + 1 == interface->process.option_window.selected_row) {
          wstandend(win);
          mvwprintw(win, rows, option_window_size - 1, " ");
        }
      }
      option_index++;
    }
  }
  wnoutrefresh(win);
}

static void update_process_option_win(struct nvtop_interface *interface) {
  unsigned int rows, cols;
  getmaxyx(interface->process.option_window.option_win, rows, cols);
  rows -= 1;
  (void)cols;
  unsigned int num_options = 0;
  switch (interface->process.option_window.state) {
  case nvtop_option_state_sort_by:
    num_options = process_field_displayed_count(interface->options.process_fields_displayed) + 1; // Option + Cancel
    break;
  case nvtop_option_state_hidden:
  default:
    break;
  }

  update_selected_offset_with_window_size(&interface->process.option_window.selected_row,
                                          &interface->process.option_window.offset, rows, num_options);

  switch (interface->process.option_window.state) {
  case nvtop_option_state_sort_by:
    draw_sort_option(interface);
    break;
  case nvtop_option_state_hidden:
  default:
    break;
  }
}

static const char *option_selection_hidden[] = {
    "Setup", "Sort", "Quit", "Save Config",
};
static const char *option_selection_hidden_num[] = {
    "2", "6", "10", "12",
};

static const char *option_selection_sort[][2] = {
    {"Enter", "Sort"},
    {"ESC", "Cancel"},
    {"+", "Ascending"},
    {"-", "Descending"},
};

static const unsigned int option_selection_width = 8;

static void draw_process_shortcuts(struct nvtop_interface *interface) {
  if (interface->process.option_window.state == interface->process.option_window.previous_state)
    return;
  WINDOW *win = interface->shortcut_window;
  enum nvtop_option_window_state current_state = interface->process.option_window.state;
  wmove(win, 0, 0);
  switch (current_state) {
  case nvtop_option_state_hidden:
    for (size_t i = 0; i < ARRAY_SIZE(option_selection_hidden); ++i) {
      if (interface->options.hide_processes_list &&
          strcmp(option_selection_hidden_num[i], "6") == 0)
        continue;

      if (process_field_displayed_count(interface->options.process_fields_displayed) > 0 || i != 1) {
        wprintw(win, "F%s", option_selection_hidden_num[i]);
        wattr_set(win, A_NORMAL, selected_color, NULL);
        wprintw(win, "%-*s", option_selection_width, option_selection_hidden[i]);
        wstandend(win);
      }
    }
    break;
  case nvtop_option_state_sort_by:
    for (size_t i = 0; i < ARRAY_SIZE(option_selection_sort); ++i) {
      wprintw(win, "%s", option_selection_sort[i][0]);
      wattr_set(win, A_NORMAL, selected_color, NULL);
      wprintw(win, "%-*s", option_selection_width, option_selection_sort[i][1]);
      wstandend(win);
    }
    break;
  default:
    break;
  }
  wclrtoeol(win);
  unsigned int cur_col, tmp;
  (void)tmp;
  getyx(win, tmp, cur_col);
  mvwchgat(win, 0, cur_col, -1, A_NORMAL, selected_color, NULL);
  wnoutrefresh(win);
  interface->process.option_window.previous_state = current_state;
}

static void draw_shortcuts(struct nvtop_interface *interface) {
  if (interface->setup_win.visible) {
    draw_setup_window_shortcuts(interface);
  } else {
    draw_process_shortcuts(interface);
  }
}

void save_current_data_to_ring(struct list_head *devices, struct nvtop_interface *interface) {
  struct gpu_info *device;
  unsigned dev_id = 0;

  list_for_each_entry(device, devices, list) {
    unsigned data_index = 0;
    for (enum plot_information info = plot_gpu_rate; info < plot_information_count; ++info) {
      if (plot_isset_draw_info(info, interface->options.gpu_specific_opts[dev_id].to_draw)) {
        unsigned data_val = INTERFACE_MISSING_VALUE;
        switch (info) {
        case plot_gpu_rate:
          if (GPUINFO_DYNAMIC_FIELD_VALID(&device->dynamic_info, gpu_util_rate))
            data_val = device->dynamic_info.gpu_util_rate;
          break;
        case plot_gpu_mem_rate:
          if (GPUINFO_DYNAMIC_FIELD_VALID(&device->dynamic_info, mem_util_rate))
            data_val = device->dynamic_info.mem_util_rate;
          break;
        case plot_encoder_rate:
          if (GPUINFO_DYNAMIC_FIELD_VALID(&device->dynamic_info, encoder_rate))
            data_val = device->dynamic_info.encoder_rate;
          break;
        case plot_decoder_rate:
          if (GPUINFO_DYNAMIC_FIELD_VALID(&device->dynamic_info, decoder_rate))
            data_val = device->dynamic_info.decoder_rate;
          break;
        case plot_gpu_temperature:
          if (GPUINFO_DYNAMIC_FIELD_VALID(&device->dynamic_info, gpu_temp)) {
            data_val = device->dynamic_info.gpu_temp;
            if (data_val > 100)
              data_val = 100u;
          }
          break;
        case plot_gpu_power_draw_rate:
          if (GPUINFO_DYNAMIC_FIELD_VALID(&device->dynamic_info, power_draw) &&
              GPUINFO_DYNAMIC_FIELD_VALID(&device->dynamic_info, power_draw_max) &&
              device->dynamic_info.power_draw_max > 0) {
            data_val = device->dynamic_info.power_draw * 100 / device->dynamic_info.power_draw_max;
            if (data_val > 100)
              data_val = 100u;
          } else {
            data_val = INTERFACE_MISSING_VALUE;
          }
          break;
        case plot_gpu_clock_rate:
          if (GPUINFO_DYNAMIC_FIELD_VALID(&device->dynamic_info, gpu_clock_speed) &&
              GPUINFO_DYNAMIC_FIELD_VALID(&device->dynamic_info, gpu_clock_speed_max) &&
              device->dynamic_info.gpu_clock_speed_max > 0) {
            data_val = device->dynamic_info.gpu_clock_speed * 100 / device->dynamic_info.gpu_clock_speed_max;
          }
          break;
        case plot_gpu_mem_clock_rate:
          if (GPUINFO_DYNAMIC_FIELD_VALID(&device->dynamic_info, mem_clock_speed) &&
              GPUINFO_DYNAMIC_FIELD_VALID(&device->dynamic_info, mem_clock_speed_max) &&
              device->dynamic_info.mem_clock_speed_max > 0) {
            data_val = device->dynamic_info.mem_clock_speed * 100 / device->dynamic_info.mem_clock_speed_max;
          }
          break;
        case plot_effective_load_rate:
          if (GPUINFO_DYNAMIC_FIELD_VALID(&device->dynamic_info, effective_load_rate)) {
            data_val = device->dynamic_info.effective_load_rate;
          }
          break;
        case plot_pcie_rx_rate:
          if (GPUINFO_DYNAMIC_FIELD_VALID(&device->dynamic_info, pcie_rx) &&
              pcie_max_bandwidth_kbs(&device->static_info))
            data_val = pcie_load_percent(device->dynamic_info.pcie_rx, &device->static_info);
          break;
        case plot_pcie_tx_rate:
          if (GPUINFO_DYNAMIC_FIELD_VALID(&device->dynamic_info, pcie_tx) &&
              pcie_max_bandwidth_kbs(&device->static_info))
            data_val = pcie_load_percent(device->dynamic_info.pcie_tx, &device->static_info);
          break;
        case plot_information_count:
          break;
        }
        interface_ring_buffer_push(&interface->saved_data_ring, dev_id, data_index, data_val);
        data_index++;
      }
    }

    dev_id++;
  }
}

static unsigned populate_plot_data_from_ring_buffer(struct list_head *devices, const struct nvtop_interface *interface,
                                                    struct plot_window *plot_win, unsigned size_data_buff,
                                                    double data[size_data_buff],
                                                    char plot_legend[MAX_LINES_PER_PLOT][PLOT_MAX_LEGEND_SIZE]) {

  for (unsigned i = 0; i < size_data_buff; ++i)
    data[i] = NAN;
  unsigned total_to_draw = 0;
  for (unsigned i = 0; i < plot_win->num_devices_to_plot; ++i) {
    unsigned dev_id = plot_win->devices_ids[i];
    plot_info_to_draw to_draw = interface->options.gpu_specific_opts[dev_id].to_draw;
    total_to_draw += plot_count_draw_info(to_draw);
  }

  assert(total_to_draw > 0);
  assert(size_data_buff % total_to_draw == 0);
  unsigned max_data_to_copy = size_data_buff / total_to_draw;
  double (*data_split)[total_to_draw] = (double (*)[total_to_draw])data;

  unsigned in_processing = 0;
  for (unsigned i = 0; i < plot_win->num_devices_to_plot; ++i) {
    unsigned dev_id = plot_win->devices_ids[i];
    plot_info_to_draw to_draw = interface->options.gpu_specific_opts[dev_id].to_draw;
    struct gpu_info *device = get_device_by_id(devices, dev_id);
    unsigned display_id = device ? device->device_id : dev_id;
    const char *unit = device ? DEVICE_UNIT_NAME(device) : "NPU";
    unsigned data_ring_index = 0;
    for (enum plot_information info = plot_gpu_rate; info < plot_information_count; ++info) {
      if (plot_isset_draw_info(info, to_draw)) {
        // Populate the legend
        switch (info) {
        case plot_gpu_rate:
          snprintf(plot_legend[in_processing], PLOT_MAX_LEGEND_SIZE, "%s%u %%", unit, display_id);
          break;
        case plot_gpu_mem_rate:
          snprintf(plot_legend[in_processing], PLOT_MAX_LEGEND_SIZE, "%s%u HBM%%", unit, display_id);
          break;
        case plot_encoder_rate:
          snprintf(plot_legend[in_processing], PLOT_MAX_LEGEND_SIZE, "%s%u encode%%", unit, display_id);
          break;
        case plot_decoder_rate:
          snprintf(plot_legend[in_processing], PLOT_MAX_LEGEND_SIZE, "%s%u decode%%", unit, display_id);
          break;
        case plot_gpu_temperature:
          snprintf(plot_legend[in_processing], PLOT_MAX_LEGEND_SIZE, "%s%u temp(c)", unit, display_id);
          break;
        case plot_gpu_power_draw_rate:
          snprintf(plot_legend[in_processing], PLOT_MAX_LEGEND_SIZE, "%s%u power%%", unit, display_id);
          break;
        case plot_gpu_clock_rate:
          snprintf(plot_legend[in_processing], PLOT_MAX_LEGEND_SIZE, "%s%u clock%%", unit, display_id);
          break;
        case plot_gpu_mem_clock_rate:
          snprintf(plot_legend[in_processing], PLOT_MAX_LEGEND_SIZE, "%s%u mem clock%%", unit, display_id);
          break;
        case plot_effective_load_rate:
          snprintf(plot_legend[in_processing], PLOT_MAX_LEGEND_SIZE, "%s%u eff. load%%", unit, display_id);
          break;
        case plot_pcie_rx_rate:
          snprintf(plot_legend[in_processing], PLOT_MAX_LEGEND_SIZE, "NPU%u PCIe RX%%", display_id);
          break;
        case plot_pcie_tx_rate:
          snprintf(plot_legend[in_processing], PLOT_MAX_LEGEND_SIZE, "NPU%u PCIe TX%%", display_id);
          break;
        case plot_information_count:
          break;
        }
        // Copy the data
        unsigned data_in_ring = interface_ring_buffer_data_stored(&interface->saved_data_ring, dev_id, data_ring_index);
        if (interface->options.plot_left_to_right) {
          for (unsigned j = 0; j < data_in_ring && j < max_data_to_copy; ++j) {
            data_split[j][in_processing] =
                interface_ring_buffer_plot_value(&interface->saved_data_ring, dev_id, data_ring_index, data_in_ring - j - 1);
          }
        } else {
          for (unsigned j = 0; j < data_in_ring && j < max_data_to_copy; ++j) {
            data_split[max_data_to_copy - j - 1][in_processing] =
                interface_ring_buffer_plot_value(&interface->saved_data_ring, dev_id, data_ring_index, data_in_ring - j - 1);
          }
        }
        data_ring_index++;
        in_processing++;
      }
    }
  }
  return total_to_draw;
}

static void draw_plots(struct list_head *devices, struct nvtop_interface *interface) {
  for (unsigned plot_id = 0; plot_id < interface->num_plots; ++plot_id) {
    werase(interface->plots[plot_id].plot_window);

    char plot_legend[MAX_LINES_PER_PLOT][PLOT_MAX_LEGEND_SIZE];

    unsigned num_lines = populate_plot_data_from_ring_buffer(devices, interface, &interface->plots[plot_id],
                                                             interface->plots[plot_id].num_data,
                                                             interface->plots[plot_id].data, plot_legend);

    nvtop_line_plot(interface->plots[plot_id].plot_window, interface->plots[plot_id].num_data,
                    interface->plots[plot_id].data, num_lines, !interface->options.plot_left_to_right, plot_legend);

    wnoutrefresh(interface->plots[plot_id].plot_window);
  }
}

void draw_gpu_info_ncurses(unsigned devices_count, struct list_head *devices, struct nvtop_interface *interface) {

  int rows, cols;
  getmaxyx(stdscr, rows, cols);
  if (device_cards_layout(interface->cards, devices, &interface->options, rows, cols,
                          interface->setup_win.visible).changed) {
    erase();
    refresh();
    delete_all_windows(interface);
    initialize_all_windows(interface);
  }
  if (interface->layout_too_small) {
    erase();
    mvprintw(0, 0, "astop: enlarge terminal to show %u NPUs", devices_count);
    mvprintw(1, 0, "Resize the terminal or press q to quit.");
    refresh();
    return;
  }
  if (!interface->setup_win.visible) {
    device_cards_draw(interface->cards, devices, &interface->options);
    draw_plots(devices, interface);
    draw_processes(devices, interface);
  } else {
    draw_setup_window(devices_count, devices, interface);
  }
  draw_shortcuts(interface);
  doupdate();
}

void update_window_size_to_terminal_size(struct nvtop_interface *inter) {
  endwin();
  erase();
  refresh();
  refresh();
  delete_all_windows(inter);
  initialize_all_windows(inter);
}

bool is_escape_for_quit(struct nvtop_interface *interface) {
  if (interface->process.option_window.state == nvtop_option_state_hidden && !interface->setup_win.visible)
    return true;
  else
    return false;
}

static void option_change_sort(struct nvtop_interface *interface) {
  if (interface->process.option_window.selected_row == 0)
    return;
  unsigned index = 0;
  for (enum process_field i = process_pid; i < process_field_count; ++i) {
    if (process_is_field_displayed(i, interface->options.process_fields_displayed)) {
      if (index == interface->process.option_window.selected_row - 1) {
        interface->options.sort_processes_by = i;
        return;
      }
      index++;
    }
  }
}

void interface_key(int keyId, struct nvtop_interface *interface) {
  if (interface->layout_too_small && keyId != KEY_F(5) && keyId != 12)
    return;
  if (interface->setup_win.visible) {
    handle_setup_win_keypress(keyId, interface);
    return;
  }
  switch (keyId) {
  case KEY_F(2):
    if (interface->process.option_window.state == nvtop_option_state_hidden && !interface->setup_win.visible) {
      show_setup_window(interface);
    }
    break;
  case KEY_F(12):
    save_interface_options_to_config_file(interface->total_dev_count, &interface->options);
    break;
  case KEY_F(6):
    if (process_field_displayed_count(interface->options.process_fields_displayed) > 0 &&
        interface->process.option_window.state == nvtop_option_state_hidden) {
      interface->process.option_window.state = nvtop_option_state_sort_by;
      interface->process.option_window.selected_row = 0;
    }
    break;
  case 'l':
  case KEY_RIGHT:
    if (interface->process.option_window.state == nvtop_option_state_hidden)
      interface->process.offset_column += 4;
    break;
  case 'h':
  case KEY_LEFT:
    if (interface->process.option_window.state == nvtop_option_state_hidden && interface->process.offset_column >= 4)
      interface->process.offset_column -= 4;
    break;
  case 'k':
  case KEY_UP:
    switch (interface->process.option_window.state) {
    case nvtop_option_state_sort_by:
      if (interface->process.option_window.selected_row != 0)
        interface->process.option_window.selected_row--;
      break;
    case nvtop_option_state_hidden:
      if (interface->process.selected_row != 0)
        interface->process.selected_row--;
      break;
    default:
      break;
    }
    break;
  case 'j':
  case KEY_DOWN:
    switch (interface->process.option_window.state) {
    case nvtop_option_state_sort_by:
      interface->process.option_window.selected_row++;
      break;
    case nvtop_option_state_hidden:
      interface->process.selected_row++;
      break;
    default:
      break;
    }
    break;
  case '+':
    interface->options.sort_descending_order = false;
    break;
  case '-':
    interface->options.sort_descending_order = true;
    break;
  case '\n':
  case KEY_ENTER:
    switch (interface->process.option_window.state) {
    case nvtop_option_state_sort_by:
      option_change_sort(interface);
      interface->process.option_window.state = nvtop_option_state_hidden;
      break;
    case nvtop_option_state_hidden:
    default:
      break;
    }
    break;
  case 27:
    interface->process.option_window.state = nvtop_option_state_hidden;
    break;
  case KEY_F(5):
  case 12: // Ctrl+L
    update_window_size_to_terminal_size(interface);
    break;
  default:
    break;
  }
}

extern inline void set_attribute_between(WINDOW *win, int startY, int startX, int endX, attr_t attr, short pair);

int interface_update_interval(const struct nvtop_interface *interface) { return interface->options.update_interval; }

unsigned interface_largest_gpu_name(struct list_head *devices) {
  struct gpu_info *gpuinfo;
  unsigned max_size = 4;
  list_for_each_entry(gpuinfo, devices, list) {
    if (GPUINFO_STATIC_FIELD_VALID(&gpuinfo->static_info, device_name)) {
      unsigned name_len = strlen(gpuinfo->static_info.device_name);
      max_size = name_len > max_size ? name_len : max_size;
    }
  }
  return max_size;
}

void interface_check_monitored_gpu_change(struct nvtop_interface **interface, unsigned allDevCount,
                                          unsigned *num_monitored_gpus, struct list_head *monitoredGpus,
                                          struct list_head *nonMonitoredGpus) {
  if (!(*interface)->setup_win.visible && (*interface)->options.has_monitored_set_changed) {
    nvtop_interface_option options_copy = (*interface)->options;
    options_copy.has_monitored_set_changed = false;
    memset(&(*interface)->options, 0, sizeof(options_copy));
    *num_monitored_gpus =
        interface_check_and_fix_monitored_gpus(allDevCount, monitoredGpus, nonMonitoredGpus, &options_copy);
    clean_ncurses(*interface);
    *interface =
        initialize_curses(allDevCount, *num_monitored_gpus, interface_largest_gpu_name(monitoredGpus), options_copy);
    timeout(interface_update_interval(*interface));
  }
}

static void print_json_string(const char *value) {
  if (!value) {
    printf("null");
    return;
  }
  putchar('"');
  for (const unsigned char *p = (const unsigned char *)value; *p; ++p) {
    if (*p == '"' || *p == '\\')
      printf("\\%c", *p);
    else if (*p < 0x20)
      printf("\\u%04x", *p);
    else
      putchar(*p);
  }
  putchar('"');
}

static void print_json_unsigned(const char *indent, const char *name, bool valid, unsigned long long value) {
  printf("%s\"%s\": ", indent, name);
  if (valid)
    printf("%llu,\n", value);
  else
    printf("null,\n");
}

static void print_json_rate(const char *indent, const char *name, bool valid, double value) {
  printf("%s\"%s\": ", indent, name);
  if (valid)
    printf("%.3f,\n", value);
  else
    printf("null,\n");
}

void print_snapshot(struct list_head *devices, bool use_fahrenheit_option, bool hide_processes_option) {
  struct gpu_info *device;

  printf("[\n");
  list_for_each_entry(device, devices, list) {
    const char *indent_level_two = "  ";
    const char *indent_level_four = "   ";
    const char *indent_level_six = "     ";
    const char *indent_level_eight = "       ";

    const char *device_name_field = "device_name";
    const char *device_architecture_field = "device_architecture";
    const char *gpu_clock_field = "npu_clock";
    const char *mem_clock_field = "mem_clock";
    const char *temp_field = "temp";
    const char *fan_field = "fan_speed";
    const char *power_field = "power_draw";
    const char *gpu_util_field = "npu_util";
    const char *mem_util_field = "mem_util";
    const char *mem_total_field = "mem_total";
    const char *mem_used_field = "mem_used";
    const char *mem_free_field = "mem_free";

    printf("%s{\n", indent_level_two);
    printf("%s\"device_id\": %u,\n", indent_level_four, device->device_id);
    printf("%s\"pci_bus_id\": ", indent_level_four);
    print_json_string(device->pdev);
    printf(",\n");

    printf("%s\"%s\": ", indent_level_four, device_name_field);
    print_json_string(GPUINFO_STATIC_FIELD_VALID(&device->static_info, device_name)
                          ? device->static_info.device_name : NULL);
    printf(",\n%s\"%s\": ", indent_level_four, device_architecture_field);
    print_json_string(GPUINFO_STATIC_FIELD_VALID(&device->static_info, device_architecture)
                          ? device->static_info.device_architecture : NULL);
    printf(",\n%s\"memory_type\": ", indent_level_four);
    print_json_string(GPUINFO_STATIC_FIELD_VALID(&device->static_info, memory_type)
                          ? device->static_info.memory_type : NULL);
    printf(",\n");

    const struct gpuinfo_dynamic_info *info = &device->dynamic_info;
    print_json_unsigned(indent_level_four, "pcie_gen", GPUINFO_DYNAMIC_FIELD_VALID(info, pcie_link_gen), info->pcie_link_gen);
    print_json_unsigned(indent_level_four, "pcie_width", GPUINFO_DYNAMIC_FIELD_VALID(info, pcie_link_width), info->pcie_link_width);
    print_json_unsigned(indent_level_four, "pcie_rx_mb_per_ms", GPUINFO_DYNAMIC_FIELD_VALID(info, pcie_rx_mb_per_ms), info->pcie_rx_mb_per_ms);
    print_json_unsigned(indent_level_four, "pcie_tx_mb_per_ms", GPUINFO_DYNAMIC_FIELD_VALID(info, pcie_tx_mb_per_ms), info->pcie_tx_mb_per_ms);
    print_json_unsigned(indent_level_four, "ecc_corrected", GPUINFO_DYNAMIC_FIELD_VALID(info, ecc_corrected), info->ecc_corrected);
    print_json_unsigned(indent_level_four, "ecc_uncorrected", GPUINFO_DYNAMIC_FIELD_VALID(info, ecc_uncorrected), info->ecc_uncorrected);
    print_json_unsigned(indent_level_four, "hccs_links", GPUINFO_DYNAMIC_FIELD_VALID(info, hccs_links), info->hccs_links);
    printf("%s\"hccs_healthy\": %s,\n", indent_level_four, GPUINFO_DYNAMIC_FIELD_VALID(info, hccs_healthy)
           ? (info->hccs_healthy ? "true" : "false") : "null");
    print_json_rate(indent_level_four, "hccs_rx_bytes_per_second", GPUINFO_DYNAMIC_FIELD_VALID(info, hccs_rx_bytes_per_second),
                    info->hccs_rx_bytes_per_second);
    print_json_rate(indent_level_four, "hccs_tx_bytes_per_second", GPUINFO_DYNAMIC_FIELD_VALID(info, hccs_tx_bytes_per_second),
                    info->hccs_tx_bytes_per_second);

    // GPU Clock Speed
    if (GPUINFO_DYNAMIC_FIELD_VALID(&device->dynamic_info, gpu_clock_speed))
      printf("%s\"%s\": \"%uMHz\",\n", indent_level_four, gpu_clock_field, device->dynamic_info.gpu_clock_speed);
    else
      printf("%s\"%s\": null,\n", indent_level_four, gpu_clock_field);

    // MEM Clock Speed
    if (GPUINFO_DYNAMIC_FIELD_VALID(&device->dynamic_info, mem_clock_speed))
      printf("%s\"%s\": \"%uMHz\",\n", indent_level_four, mem_clock_field, device->dynamic_info.mem_clock_speed);
    else
      printf("%s\"%s\": null,\n", indent_level_four, mem_clock_field);

    // Vendor specific clock domains, keyed by the name the vendor gives them
    unsigned extra_clock_count =
        GPUINFO_DYNAMIC_FIELD_VALID(&device->dynamic_info, extra_clocks) ? device->dynamic_info.extra_clock_count : 0;
    for (unsigned i = 0; i < extra_clock_count; ++i) {
      const struct gpuinfo_extra_clock *clock = &device->dynamic_info.extra_clocks[i];
      printf("%s\"%s_clock\": \"%uMHz\",\n", indent_level_four, clock->name, clock->speed);
    }

    // GPU Temperature
    if (GPUINFO_DYNAMIC_FIELD_VALID(&device->dynamic_info, gpu_temp)) {
      unsigned int temp_convert;
      if (!use_fahrenheit_option)
        temp_convert = device->dynamic_info.gpu_temp;
      else
        temp_convert = (unsigned)(32 + nearbyint(device->dynamic_info.gpu_temp * 1.8));

      printf("%s\"%s\": \"%u%s\",\n", indent_level_four, temp_field, temp_convert, use_fahrenheit_option ? "F" : "C");
    } else {
      printf("%s\"%s\": null,\n", indent_level_four, temp_field);
    }

    // Fan speed
    if (GPUINFO_DYNAMIC_FIELD_VALID(&device->dynamic_info, fan_speed))
      printf("%s\"%s\": \"%u%%\",\n", indent_level_four, fan_field,
             device->dynamic_info.fan_speed > 100 ? 100 : device->dynamic_info.fan_speed);
    else if (GPUINFO_DYNAMIC_FIELD_VALID(&device->dynamic_info, fan_rpm))
      printf("%s\"%s\": \"%uRPM\",\n", indent_level_four, fan_field,
             device->dynamic_info.fan_rpm > 9999 ? 9999 : device->dynamic_info.fan_rpm);
    else if (device->static_info.integrated_graphics)
      printf("%s\"%s\": \"CPU Fan\",\n", indent_level_four, fan_field);
    else
      printf("%s\"%s\": null,\n", indent_level_four, fan_field);

    // Power draw
    if (GPUINFO_DYNAMIC_FIELD_VALID(&device->dynamic_info, power_draw))
      printf("%s\"%s\": \"%uW\",\n", indent_level_four, power_field, device->dynamic_info.power_draw / 1000);
    else
      printf("%s\"%s\": null,\n", indent_level_four, power_field);

    // GPU Utilization
    if (GPUINFO_DYNAMIC_FIELD_VALID(&device->dynamic_info, gpu_util_rate))
      printf("%s\"%s\": \"%u%%\",\n", indent_level_four, gpu_util_field, device->dynamic_info.gpu_util_rate);
    else
      printf("%s\"%s\": null,\n", indent_level_four, gpu_util_field);

    // Encode / Decode
    if (device->static_info.encode_decode_shared) {
      printf("%s\"encode_decode\": ", indent_level_four);
      if (GPUINFO_DYNAMIC_FIELD_VALID(&device->dynamic_info, decoder_rate))
        printf("\"%u%%\",\n", device->dynamic_info.decoder_rate);
      else
        printf("null,\n");
    } else {
      printf("%s\"encode\": ", indent_level_four);
      if (GPUINFO_DYNAMIC_FIELD_VALID(&device->dynamic_info, encoder_rate))
        printf("\"%u%%\",\n", device->dynamic_info.encoder_rate);
      else
        printf("null,\n");
      printf("%s\"decode\": ", indent_level_four);
      if (GPUINFO_DYNAMIC_FIELD_VALID(&device->dynamic_info, decoder_rate))
        printf("\"%u%%\",\n", device->dynamic_info.decoder_rate);
      else
        printf("null,\n");
    }

    // Memory Utilization
    if (GPUINFO_DYNAMIC_FIELD_VALID(&device->dynamic_info, mem_util_rate))
      printf("%s\"%s\": \"%u%%\",\n", indent_level_four, mem_util_field, device->dynamic_info.mem_util_rate);
    else
      printf("%s\"%s\": null,\n", indent_level_four, mem_util_field);
    // Memory Total
    if (GPUINFO_DYNAMIC_FIELD_VALID(&device->dynamic_info, total_memory))
      printf("%s\"%s\": \"%llu\",\n", indent_level_four, mem_total_field, device->dynamic_info.total_memory);
    else
      printf("%s\"%s\": null,\n", indent_level_four, mem_total_field);
    // Memory Used
    if (GPUINFO_DYNAMIC_FIELD_VALID(&device->dynamic_info, used_memory))
      printf("%s\"%s\": \"%llu\",\n", indent_level_four, mem_used_field, device->dynamic_info.used_memory);
    else
      printf("%s\"%s\": null,\n", indent_level_four, mem_used_field);
    // Memory Available
    if (GPUINFO_DYNAMIC_FIELD_VALID(&device->dynamic_info, free_memory))
      printf("%s\"%s\": \"%llu\"", indent_level_four, mem_free_field, device->dynamic_info.free_memory);
    else
      printf("%s\"%s\": null", indent_level_four, mem_free_field);

    // Processes
    if (hide_processes_option) {
      // (Notice: no comma at the end as it's the last field here)
      printf("\n");
    } else {
      printf(",\n%s\"processes\" : [\n", indent_level_four);
      for (unsigned i = 0; i < device->processes_count; ++i) {
        struct gpu_process *proc = &device->processes[i];
        printf("%s{\n", indent_level_six);

        // PID
        printf("%s\"pid\": \"%d\",\n", indent_level_eight, proc->pid);

        printf("%s\"cmdline\": ", indent_level_eight);
        print_json_string(GPUINFO_PROCESS_FIELD_VALID(proc, cmdline) ? proc->cmdline : NULL);
        printf(",\n");

        printf("%s\"kind\": ", indent_level_eight);
        if (proc->type != gpu_process_unknown) {
          printf("\"");
          switch (proc->type) {
          case gpu_process_graphical:
            printf("graphic");
            break;
          case gpu_process_compute:
            printf("compute");
            break;
          case gpu_process_graphical_compute:
            printf("graphic & compute");
            break;
          default:
            printf("N/A");
            break;
          }
          printf("\"");
        } else {
          printf("null");
        }
        printf(",\n");

        // GPU memory usage
        printf("%s\"user\": ", indent_level_eight);
        print_json_string(GPUINFO_PROCESS_FIELD_VALID(proc, user_name) ? proc->user_name : NULL);
        printf(",\n");

        // GPU usage
        printf("%s\"npu_usage\": ", indent_level_eight);
        if (GPUINFO_PROCESS_FIELD_VALID(proc, gpu_usage))
          printf("\"%u%%\",\n", proc->gpu_usage);
        else
          printf("null,\n");

        // GPU memory usage
        printf("%s\"npu_mem_bytes_alloc\": ", indent_level_eight);
        if (GPUINFO_PROCESS_FIELD_VALID(proc, gpu_memory_usage))
          printf("\"%llu\",\n", proc->gpu_memory_usage);
        else
          printf("null,\n");

        // GPU memory usage
        printf("%s\"npu_mem_usage\": ", indent_level_eight);
        if (GPUINFO_PROCESS_FIELD_VALID(proc, gpu_memory_percentage))
          printf("\"%u%%\",\n", proc->gpu_memory_percentage);
        else
          printf("null,\n");

        // Encode usage
        if (device->static_info.encode_decode_shared) {
          // (Notice: no comma at the end as it's the last field here)
          printf("%s\"encode_decode\": ", indent_level_eight);
          if (GPUINFO_PROCESS_FIELD_VALID(proc, decode_usage))
            printf("\"%u%%\"\n", proc->decode_usage);
          else
            printf("null\n");
        } else {
          printf("%s\"encode\": ", indent_level_eight);
          if (GPUINFO_PROCESS_FIELD_VALID(proc, encode_usage))
            printf("\"%u%%\",\n", proc->encode_usage);
          else
            printf("null,\n");
          // (Notice: no comma at the end as it's the last field here)
          printf("%s\"decode\": ", indent_level_eight);
          if (GPUINFO_PROCESS_FIELD_VALID(proc, decode_usage))
            printf("\"%u%%\"\n", proc->decode_usage);
          else
            printf("null\n");
        }

        printf("%s}", indent_level_six);
        if (i != device->processes_count - 1)
          printf(",");
        printf("\n");
      }
      // (Notice: no comma at the end as it's the last field here)
      printf("%s]\n", indent_level_four);
    }

    if (device->list.next == devices)
      printf("%s}\n", indent_level_two);
    else
      printf("%s},\n", indent_level_two);
  }
  printf("]\n");
  fflush(stdout);
}
