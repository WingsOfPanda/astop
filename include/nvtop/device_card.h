#ifndef DEVICE_CARD_H__
#define DEVICE_CARD_H__

#include "nvtop/interface_layout_selection.h"

struct device_cards;

struct device_card_layout {
  unsigned rows, cols;
  bool too_small;
  bool changed; // Fields or options require rebuilding the active layout.
};

struct device_cards *device_cards_create(unsigned count);
void device_cards_destroy(struct device_cards *cards);

// NULL devices preserves measured fields and recomputes fit for allocation or resize;
// callers rebuild windows on resize even when the minimum card dimensions are unchanged.
// Deferred refreshes leave the active layout intact until setup closes.
struct device_card_layout device_cards_layout(struct device_cards *cards, struct list_head *devices,
                                              const nvtop_interface_option *options, unsigned rows,
                                              unsigned cols, bool defer);

// Both preflight and the overall layout use this centered grid with fixed gutters.
unsigned device_cards_place(unsigned count, unsigned card_rows, unsigned min_cols, unsigned cols,
                            bool separate_rows, struct window_position *positions);

// Allocate after a fitting layout and placement; release before rebuilding.
void device_cards_allocate(struct device_cards *cards, const struct window_position *positions);
void device_cards_release_windows(struct device_cards *cards);
void device_cards_draw(struct device_cards *cards, struct list_head *devices, const nvtop_interface_option *options);

#endif
