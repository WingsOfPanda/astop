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

#include "nvtop/device_card.h"
#include "nvtop/interface_internal_common.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tgmath.h>

#define EXTRA_INFO_PER_ROW 2
#define MAX_EXTRA_INFO_ITEMS 16
#define LINK_INFO_FIELDS 8
#define COMPACT_DEVICE_COLS 38
#define DETAILED_DEVICE_COLS 82
#define HCCS_RATE_COLS 17
#define PRIMARY_INFO_COLS 14

struct device_window {
  WINDOW *name_win;
  WINDOW *gpu_util_no_enc_and_dec;
  WINDOW *mem_util_no_enc_and_dec;
  WINDOW *temperature;
  WINDOW *power_info;
  WINDOW *metadata;
  WINDOW *extra_info[MAX_EXTRA_INFO_ITEMS];
};

struct device_cards {
  unsigned count;
  struct device_card_layout layout;
  unsigned extra_rows;
  unsigned detailed_cols;
  unsigned char option_sig;
  struct device_window windows[];
};

static unsigned cards_per_row(unsigned devices_count, unsigned min_card_width, unsigned cols) {
  unsigned per_row = max(1, (cols + 2) / (min_card_width + 2));
  unsigned stacks = max(1, (devices_count + per_row - 1) / per_row);
  if (devices_count % stacks == 0)
    per_row = devices_count / stacks;
  return per_row;
}

unsigned device_cards_place(unsigned count, unsigned card_rows, unsigned min_cols, unsigned cols,
                            bool separate_rows, struct window_position *positions) {
  unsigned per_row = cards_per_row(count, min_cols, cols);
  unsigned gutter_cols = 2 * (per_row - 1);
  unsigned width = (cols - gutter_cols) / per_row;
  unsigned margin = (cols - gutter_cols - per_row * width) / 2;
  for (unsigned i = 0; i < count; ++i)
    positions[i] = (struct window_position){.posX = margin + i % per_row * (width + 2),
        .posY = i / per_row * (card_rows + separate_rows), .sizeX = width, .sizeY = card_rows};
  return (count + per_row - 1) / per_row;
}

// Compact cards keep all eight devices visible on an 80-column terminal.
static void alloc_device_window(unsigned start_row, unsigned start_col, unsigned cols,
                                unsigned extra_info_rows,
                                struct device_window *dwin) {
  memset(dwin, 0, sizeof(*dwin));
  dwin->name_win = newwin(1, cols, start_row, start_col);
  dwin->temperature = newwin(1, 10, start_row + 1, start_col);
  dwin->power_info = newwin(1, 13, start_row + 1, start_col + 10);
  dwin->metadata = newwin(1, cols - 23, start_row + 1, start_col + 23);
  unsigned bar_cols = (cols - 1) / 2;
  dwin->gpu_util_no_enc_and_dec = newwin(1, bar_cols, start_row + 2, start_col);
  dwin->mem_util_no_enc_and_dec = newwin(1, cols - bar_cols - 1, start_row + 2, start_col + bar_cols + 1);
  if (!dwin->name_win || !dwin->temperature || !dwin->power_info || !dwin->metadata ||
      !dwin->gpu_util_no_enc_and_dec || !dwin->mem_util_no_enc_and_dec) {
    endwin();
    fprintf(stderr, "Error: terminal too small for device cards\n");
    exit(EXIT_FAILURE);
  }
  for (unsigned i = 0; i < min(extra_info_rows * EXTRA_INFO_PER_ROW, MAX_EXTRA_INFO_ITEMS); ++i)
    dwin->extra_info[i] = newwin(1, cols / 2, start_row + 3 + i / EXTRA_INFO_PER_ROW,
                               start_col + i % EXTRA_INFO_PER_ROW * (cols / 2));
}

static void free_device_windows(struct device_window *dwin) {
  delwin(dwin->name_win);
  delwin(dwin->temperature);
  delwin(dwin->power_info);
  delwin(dwin->metadata);
  delwin(dwin->gpu_util_no_enc_and_dec);
  delwin(dwin->mem_util_no_enc_and_dec);
  for (unsigned i = 0; i < MAX_EXTRA_INFO_ITEMS; ++i)
    if (dwin->extra_info[i])
      delwin(dwin->extra_info[i]);
}

static unsigned device_length(unsigned devices, unsigned rows, unsigned cols, unsigned extra_rows,
                              unsigned detailed_cols, bool force) {
  detailed_cols = max(DETAILED_DEVICE_COLS, detailed_cols);
  unsigned per_row = cards_per_row(devices, detailed_cols, cols);
  unsigned stacks = (devices + per_row - 1) / per_row;
  unsigned process_rows = 1 + max(5, min(rows / 4, devices * 3));
  return force || (cols >= detailed_cols && stacks * (3 + extra_rows) + process_rows + 1 <= rows)
             ? detailed_cols : COMPACT_DEVICE_COLS;
}

struct device_cards *device_cards_create(unsigned count) {
  struct device_cards *cards = calloc(1, sizeof(*cards) + count * sizeof(cards->windows[0]));
  if (!cards) {
    perror("Cannot allocate device cards");
    exit(EXIT_FAILURE);
  }
  cards->count = count;
  return cards;
}

void device_cards_release_windows(struct device_cards *cards) {
  for (unsigned i = 0; i < cards->count; ++i) {
    if (cards->windows[i].name_win)
      free_device_windows(&cards->windows[i]);
    memset(&cards->windows[i], 0, sizeof(cards->windows[i]));
  }
}

void device_cards_destroy(struct device_cards *cards) {
  device_cards_release_windows(cards);
  free(cards);
}

void device_cards_allocate(struct device_cards *cards, const struct window_position *positions) {
  for (unsigned i = 0; i < cards->count; ++i)
    alloc_device_window(positions[i].posY, positions[i].posX, positions[i].sizeX,
                        cards->extra_rows, &cards->windows[i]);
}

static void draw_percentage_meter(WINDOW *win, const char *prelude, unsigned int new_percentage,
                                  const char *inside_braces_right) {
  int rows, cols;
  getmaxyx(win, rows, cols);
  (void)rows;
  new_percentage = min(new_percentage, 100);
  size_t size_prelude = strlen(prelude);
  wcolor_set(win, cyan_color, NULL);
  mvwprintw(win, 0, 0, "%s", prelude);
  wstandend(win);
  waddch(win, '[');
  int curx, cury;
  curx = getcurx(win);
  cury = getcury(win);
  int between_sbraces = cols - size_prelude - 2;
  float usage = round((float)between_sbraces * new_percentage / 100.f);
  int represent_usage = (int)usage;
  whline(win, '|', (int)represent_usage);
  mvwhline(win, cury, curx + represent_usage, ' ', between_sbraces - represent_usage);
  mvwaddch(win, cury, curx + between_sbraces, ']');
  unsigned int right_side_braces_space_required = strlen(inside_braces_right);
  wmove(win, cury, curx + between_sbraces - right_side_braces_space_required);
  wprintw(win, "%s", inside_braces_right);
  mvwchgat(win, cury, curx, represent_usage, 0, green_color, NULL);
  wnoutrefresh(win);
}

static void draw_temp_color(WINDOW *win, unsigned int temp, unsigned int temp_slowdown, bool celsius) {
  unsigned int temp_convert;
  if (celsius)
    temp_convert = temp;
  else
    temp_convert = (unsigned)(32 + nearbyint(temp * 1.8));
  wcolor_set(win, cyan_color, NULL);
  mvwprintw(win, 0, 0, "TEMP");

  if (temp >= temp_slowdown - 5) {
    if (temp >= temp_slowdown) {
      wcolor_set(win, warning_color, NULL);
      wattron(win, A_BOLD);
    } else
      wcolor_set(win, yellow_color, NULL);
  } else {
    wcolor_set(win, green_color, NULL);
  }
  wprintw(win, " %3u", temp_convert);
  wstandend(win);

  waddch(win, ACS_DEGREE);
  if (celsius)
    waddch(win, 'C');
  else
    waddch(win, 'F');
  wnoutrefresh(win);
}

// A cell of the extra GPU info bar. Fields carry no assumption about what they
// represent: a short name and an already-formatted value, with the unit folded
// in when there is one (e.g. "1980MHz" or a plain "128"). A dedicated unit
// field can be split out later if a consumer needs the number on its own.
#define EXTRA_INFO_NAME_LEN 8
#define EXTRA_INFO_VALUE_LEN 16

struct extra_info_item {
  char name[EXTRA_INFO_NAME_LEN];
  char value[EXTRA_INFO_VALUE_LEN];
};

static void add_extra_info_item(struct extra_info_item *items, unsigned *count, unsigned max_items, const char *name,
                                unsigned number, const char *unit) {
  if (*count >= max_items)
    return;
  struct extra_info_item *item = &items[*count];
  snprintf(item->name, sizeof(item->name), "%s", name);
  snprintf(item->value, sizeof(item->value), "%u%s", number, unit);
  (*count)++;
}

// Primary clocks share the two metadata lines; only -X adds rows below the meters.
static unsigned collect_primary_info(const struct gpu_info *device, const nvtop_interface_option *options,
                                   struct extra_info_item *items, unsigned max_items) {
  unsigned count = 0;
  const struct gpuinfo_dynamic_info *dynamic_info = &device->dynamic_info;

  if (options->has_gpu_info_bar) {
    add_extra_info_item(items, &count, max_items, "NPU", dynamic_info->gpu_clock_speed, "MHz");
    if (!GPUINFO_DYNAMIC_FIELD_VALID(dynamic_info, gpu_clock_speed))
      snprintf(items[count - 1].value, sizeof(items[count - 1].value), "N/A");
    add_extra_info_item(items, &count, max_items, "HBM", dynamic_info->mem_clock_speed, "MHz");
    if (!GPUINFO_DYNAMIC_FIELD_VALID(dynamic_info, mem_clock_speed))
      snprintf(items[count - 1].value, sizeof(items[count - 1].value), "N/A");
    if (GPUINFO_DYNAMIC_FIELD_VALID(dynamic_info, encoder_rate))
      add_extra_info_item(items, &count, max_items, "ENC", dynamic_info->encoder_rate, "%");
    if (GPUINFO_DYNAMIC_FIELD_VALID(dynamic_info, decoder_rate))
      add_extra_info_item(items, &count, max_items, "DEC", dynamic_info->decoder_rate, "%");
  }

  if (options->has_gpu_info_bar && IS_VALID(gpuinfo_extra_clocks_valid, dynamic_info->valid)) {
    for (unsigned i = 0; i < dynamic_info->extra_clock_count; ++i) {
      if (dynamic_info->extra_clocks[i].secondary)
        continue;
      add_extra_info_item(items, &count, max_items, dynamic_info->extra_clocks[i].name,
                          dynamic_info->extra_clocks[i].speed, "MHz");
    }
  }

  return count;
}

static unsigned collect_extra_info(const struct gpu_info *device, const nvtop_interface_option *options,
                                   struct extra_info_item *items, unsigned max_items) {
  unsigned count = 0;
  const struct gpuinfo_dynamic_info *dynamic_info = &device->dynamic_info;
  const struct gpuinfo_static_info *static_info = &device->static_info;
  if (options->has_all_info_bar) {
    if (GPUINFO_STATIC_FIELD_VALID(static_info, n_shared_cores))
      add_extra_info_item(items, &count, max_items, "NSHC", static_info->n_shared_cores, "");
    if (GPUINFO_STATIC_FIELD_VALID(static_info, l2cache_size))
      add_extra_info_item(items, &count, max_items, "L2CF", static_info->l2cache_size, "");
    if (GPUINFO_STATIC_FIELD_VALID(static_info, n_exec_engines))
      add_extra_info_item(items, &count, max_items, "NEXC", static_info->n_exec_engines, "");
    if (IS_VALID(gpuinfo_extra_clocks_valid, dynamic_info->valid)) {
      for (unsigned i = 0; i < dynamic_info->extra_clock_count; ++i) {
        if (!dynamic_info->extra_clocks[i].secondary)
          continue;
        add_extra_info_item(items, &count, max_items, dynamic_info->extra_clocks[i].name,
                            dynamic_info->extra_clocks[i].speed, "MHz");
      }
    }
  }

  return count;
}

struct device_card_layout device_cards_layout(struct device_cards *cards, struct list_head *devices,
                                              const nvtop_interface_option *options, unsigned rows,
                                              unsigned cols, bool defer) {
  unsigned extra_rows = cards->extra_rows;
  unsigned detailed_cols = cards->detailed_cols;
  unsigned char option_sig = (options->has_gpu_info_bar ? 1u : 0u) | (options->has_all_info_bar ? 2u : 0u);
  bool options_changed = option_sig != cards->option_sig;
  if (devices) {
    unsigned most_fields = 0;
    detailed_cols = DETAILED_DEVICE_COLS;
    struct extra_info_item items[MAX_EXTRA_INFO_ITEMS];
    struct gpu_info *device;
    list_for_each_entry(device, devices, list) {
      unsigned visible = collect_extra_info(device, options, items, MAX_EXTRA_INFO_ITEMS);
      most_fields = max(most_fields, visible);
      unsigned primary = collect_primary_info(device, options, items, MAX_EXTRA_INFO_ITEMS);
      detailed_cols = max(detailed_cols, 64 + primary * PRIMARY_INFO_COLS +
          (GPUINFO_DYNAMIC_FIELD_VALID(&device->dynamic_info, fan_rpm) ? 12 : 0));
    }
    extra_rows = (most_fields + EXTRA_INFO_PER_ROW - 1) / EXTRA_INFO_PER_ROW;
    // Failed reads keep their space; an option change may give it back.
    if (!options_changed) {
      extra_rows = max(extra_rows, cards->extra_rows);
      detailed_cols = max(detailed_cols, cards->detailed_cols);
    }
  } else {
    detailed_cols = max(detailed_cols, options->has_gpu_info_bar ? 64 + 2 * PRIMARY_INFO_COLS : 0);
  }
  unsigned header_cols = device_length(cards->count, rows, cols, extra_rows, detailed_cols,
                                       options->has_gpu_info_bar);
  bool changed = extra_rows != cards->extra_rows || header_cols != cards->layout.cols || options_changed;
  if (devices && (!changed || defer)) {
    struct device_card_layout layout = cards->layout;
    layout.changed = false;
    return layout;
  }
  unsigned per_row = cards_per_row(cards->count, header_cols, cols);
  unsigned stacks = (cards->count + per_row - 1) / per_row;
  cards->extra_rows = extra_rows;
  cards->detailed_cols = detailed_cols;
  // A resize does not consume options still pending behind the setup window.
  if (devices)
    cards->option_sig = option_sig;
  cards->layout = (struct device_card_layout){.rows = 3 + extra_rows, .cols = header_cols,
      .too_small = cols < 40 || cols < header_cols || rows < stacks * (3 + extra_rows) + 5,
      .changed = changed};
  return cards->layout;
}

static void format_link_rate(char *text, size_t size, const char *direction, bool valid, double bytes_per_second) {
  if (!valid) {
    snprintf(text, size, "%s N/A", direction);
    return;
  }
  const char *unit = "KiB/s";
  double rate = bytes_per_second / 1024.;
  if (rate >= 1024.) {
    rate /= 1024.;
    unit = "MiB/s";
  }
  if (rate >= 1024.) {
    rate /= 1024.;
    unit = "GiB/s";
  }
  snprintf(text, size, "%s %.3f%s", direction, rate, unit);
}

#define LINK_INFO_CELL_SIZE 64
static void format_link_info(const struct gpuinfo_dynamic_info *info, char cells[LINK_INFO_FIELDS][LINK_INFO_CELL_SIZE]) {
  snprintf(cells[0], LINK_INFO_CELL_SIZE, "PCIe N/A");
  if (GPUINFO_DYNAMIC_FIELD_VALID(info, pcie_link_gen) && GPUINFO_DYNAMIC_FIELD_VALID(info, pcie_link_width))
    snprintf(cells[0], LINK_INFO_CELL_SIZE, "PCIe GEN%u@%ux", info->pcie_link_gen, info->pcie_link_width);
  char corrected[24] = "N/A", uncorrected[24] = "N/A";
  if (GPUINFO_DYNAMIC_FIELD_VALID(info, ecc_corrected))
    snprintf(corrected, sizeof(corrected), "%llu", info->ecc_corrected);
  if (GPUINFO_DYNAMIC_FIELD_VALID(info, ecc_uncorrected))
    snprintf(uncorrected, sizeof(uncorrected), "%llu", info->ecc_uncorrected);
  snprintf(cells[1], LINK_INFO_CELL_SIZE, "ECC %s/%s", corrected, uncorrected);
  snprintf(cells[2], LINK_INFO_CELL_SIZE, "RX N/A");
  if (GPUINFO_DYNAMIC_FIELD_VALID(info, pcie_rx_mb_per_ms))
    snprintf(cells[2], LINK_INFO_CELL_SIZE, "RX %lluMB/ms", info->pcie_rx_mb_per_ms);
  snprintf(cells[3], LINK_INFO_CELL_SIZE, "TX N/A");
  if (GPUINFO_DYNAMIC_FIELD_VALID(info, pcie_tx_mb_per_ms))
    snprintf(cells[3], LINK_INFO_CELL_SIZE, "TX %lluMB/ms", info->pcie_tx_mb_per_ms);
  snprintf(cells[4], LINK_INFO_CELL_SIZE, "HCCS N/A");
  if (GPUINFO_DYNAMIC_FIELD_VALID(info, hccs_links))
    snprintf(cells[4], LINK_INFO_CELL_SIZE, "HCCS %ux", info->hccs_links);
  snprintf(cells[5], LINK_INFO_CELL_SIZE, "%s", GPUINFO_DYNAMIC_FIELD_VALID(info, hccs_healthy)
           ? (info->hccs_healthy ? "OK" : "WARN") : "N/A");
  format_link_rate(cells[6], LINK_INFO_CELL_SIZE, "RX", GPUINFO_DYNAMIC_FIELD_VALID(info, hccs_rx_bytes_per_second),
                   info->hccs_rx_bytes_per_second);
  format_link_rate(cells[7], LINK_INFO_CELL_SIZE, "TX", GPUINFO_DYNAMIC_FIELD_VALID(info, hccs_tx_bytes_per_second),
                   info->hccs_tx_bytes_per_second);
}

static void fit_link_cell(char *text, unsigned columns) {
  assert(columns >= 3);
  if (strlen(text) > columns)
    strcpy(text + columns - 3, "...");
}

static void draw_header_field(WINDOW *win, unsigned column, unsigned width, const char *value) {
  char text[1024];
  snprintf(text, sizeof(text), "%.*s", (int)sizeof(text) - 1, value);
  fit_link_cell(text, width - 1);
  mvwaddnstr(win, 0, column, text, width - 1);
}

void device_cards_draw(struct device_cards *cards, struct list_head *devices, const nvtop_interface_option *options) {
  struct gpu_info *device;
  unsigned dev_id = 0;
  list_for_each_entry(device, devices, list) {
    struct device_window *dev = &cards->windows[dev_id];
    bool detailed = cards->layout.cols >= DETAILED_DEVICE_COLS;
    char buff[1024];
    werase(dev->name_win);
    wcolor_set(dev->name_win, cyan_color, NULL);
    snprintf(buff, sizeof(buff), "NPU %-2u [%s]", device->device_id,
             GPUINFO_STATIC_FIELD_VALID(&device->static_info, device_name) ? device->static_info.device_name : "N/A");
    draw_header_field(dev->name_win, 0, detailed ? 24 : getmaxx(dev->name_win), buff);
    wstandend(dev->name_win);
    werase(dev->metadata);
    if (detailed) {
      char cells[LINK_INFO_FIELDS][LINK_INFO_CELL_SIZE];
      format_link_info(&device->dynamic_info, cells);
      draw_header_field(dev->name_win, 24, 14, cells[0]);
      draw_header_field(dev->name_win, 38, 13, cells[2]);
      draw_header_field(dev->name_win, 51, 13, cells[3]);
      draw_header_field(dev->metadata, 0, 12, cells[1]);
      snprintf(buff, sizeof(buff), "%s %s", cells[4], cells[5]);
      if (GPUINFO_DYNAMIC_FIELD_VALID(&device->dynamic_info, hccs_healthy) && !device->dynamic_info.hccs_healthy)
        wcolor_set(dev->metadata, warning_color, NULL);
      draw_header_field(dev->metadata, 12, 13, buff);
      wstandend(dev->metadata);
      draw_header_field(dev->metadata, 25, HCCS_RATE_COLS, cells[6]);
      draw_header_field(dev->metadata, 25 + HCCS_RATE_COLS, HCCS_RATE_COLS, cells[7]);
      struct extra_info_item primary_items[MAX_EXTRA_INFO_ITEMS];
      unsigned primary_count = collect_primary_info(device, options, primary_items, MAX_EXTRA_INFO_ITEMS);
      for (unsigned i = 0; i < primary_count; ++i) {
        snprintf(buff, sizeof(buff), "%s %s", primary_items[i].name, primary_items[i].value);
        draw_header_field(dev->name_win, 64 + i * PRIMARY_INFO_COLS, PRIMARY_INFO_COLS, buff);
      }
      if (GPUINFO_DYNAMIC_FIELD_VALID(&device->dynamic_info, fan_rpm)) {
        snprintf(buff, sizeof(buff), "FAN %uRPM", device->dynamic_info.fan_rpm);
        draw_header_field(dev->name_win, 64 + primary_count * PRIMARY_INFO_COLS, 12, buff);
      }
    } else if (GPUINFO_DYNAMIC_FIELD_VALID(&device->dynamic_info, fan_rpm)) {
      snprintf(buff, sizeof(buff), "FAN %uRPM", device->dynamic_info.fan_rpm);
      draw_header_field(dev->metadata, 0, getmaxx(dev->metadata), buff);
    }
    wnoutrefresh(dev->name_win);
    wnoutrefresh(dev->metadata);
    if (GPUINFO_DYNAMIC_FIELD_VALID(&device->dynamic_info, gpu_util_rate)) {
      snprintf(buff, sizeof(buff), "%u%%", device->dynamic_info.gpu_util_rate);
      draw_percentage_meter(dev->gpu_util_no_enc_and_dec, "NPU", device->dynamic_info.gpu_util_rate, buff);
    } else {
      draw_percentage_meter(dev->gpu_util_no_enc_and_dec, "NPU", 0, "N/A");
    }
    if (GPUINFO_DYNAMIC_FIELD_VALID(&device->dynamic_info, total_memory) &&
        GPUINFO_DYNAMIC_FIELD_VALID(&device->dynamic_info, used_memory) && device->dynamic_info.total_memory) {
      snprintf(buff, sizeof(buff), "%.1f/%.1fGi", device->dynamic_info.used_memory / (1024. * 1024. * 1024.),
               device->dynamic_info.total_memory / (1024. * 1024. * 1024.));
      unsigned percent = (unsigned)(100. * device->dynamic_info.used_memory / device->dynamic_info.total_memory);
      draw_percentage_meter(dev->mem_util_no_enc_and_dec, "HBM", min(percent, 100), buff);
    } else {
      draw_percentage_meter(dev->mem_util_no_enc_and_dec, "HBM", 0, "N/A");
    }
    werase(dev->temperature);
    if (GPUINFO_DYNAMIC_FIELD_VALID(&device->dynamic_info, gpu_temp)) {
      unsigned threshold = GPUINFO_STATIC_FIELD_VALID(&device->static_info, temperature_slowdown_threshold)
          ? device->static_info.temperature_slowdown_threshold : UINT_MAX;
      draw_temp_color(dev->temperature, device->dynamic_info.gpu_temp, threshold,
                      !options->temperature_in_fahrenheit);
    } else {
      mvwprintw(dev->temperature, 0, 0, "TEMP N/A");
      wnoutrefresh(dev->temperature);
    }
    werase(dev->power_info);
    if (GPUINFO_DYNAMIC_FIELD_VALID(&device->dynamic_info, power_draw))
      mvwprintw(dev->power_info, 0, 0, " POW %.1fW", device->dynamic_info.power_draw / 1000.);
    else
      mvwprintw(dev->power_info, 0, 0, " POW N/A");
    wnoutrefresh(dev->power_info);
    struct extra_info_item extra_items[MAX_EXTRA_INFO_ITEMS];
    unsigned extra_count = collect_extra_info(device, options, extra_items, MAX_EXTRA_INFO_ITEMS);
    for (unsigned i = 0; i < MAX_EXTRA_INFO_ITEMS && dev->extra_info[i]; ++i) {
      werase(dev->extra_info[i]);
      if (i < extra_count)
        mvwprintw(dev->extra_info[i], 0, 0, "%s %s", extra_items[i].name, extra_items[i].value);
      wnoutrefresh(dev->extra_info[i]);
    }
    dev_id++;
  }
}
