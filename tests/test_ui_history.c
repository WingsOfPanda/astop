#include "nvtop/interface_ring_buffer.h"
#include "nvtop/plot.h"

#include <assert.h>
#include <math.h>
#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
  interface_ring_buffer ring;
  interface_alloc_ring_buffer(1, 1, 4, &ring);
  interface_ring_buffer_push(&ring, 0, 0, 10);
  interface_ring_buffer_push(&ring, 0, 0, INTERFACE_MISSING_VALUE);
  interface_ring_buffer_push(&ring, 0, 0, 30);
  interface_ring_buffer_push(&ring, 0, 0, 40);
  assert(interface_ring_buffer_data_stored(&ring, 0, 0) == 3);
  assert(isnan(interface_ring_buffer_plot_value(&ring, 0, 0, 0)));
  assert(interface_ring_buffer_plot_value(&ring, 0, 0, 1) == 30.);
  assert(interface_ring_buffer_plot_value(&ring, 0, 0, 2) == 40.);
  interface_free_ring_buffer(&ring);

  FILE *output = tmpfile();
  FILE *input = tmpfile();
  assert(output && input);
  SCREEN *screen = newterm("xterm-256color", output, input);
  assert(screen);
  WINDOW *window = newwin(7, 16, 0, 0);
  assert(window);
  const double samples[] = {50., NAN, 80., NAN};
  char legend[MAX_LINES_PER_PLOT][PLOT_MAX_LEGEND_SIZE] = {{0}};
  nvtop_line_plot(window, 4, samples, 1, false, legend);
  assert((mvwinch(window, 3, 0) & A_CHARTEXT) != ' ');
  assert((mvwinch(window, 1, 2) & A_CHARTEXT) != ' ');
  for (int row = 0; row < 7; ++row) {
    assert((mvwinch(window, row, 1) & A_CHARTEXT) == ' ');
    assert((mvwinch(window, row, 3) & A_CHARTEXT) == ' ');
    assert((mvwinch(window, row, 4) & A_CHARTEXT) == ' ');
  }
  // The new reading starts at its own level, without bridging the missing sample.
  assert((mvwinch(window, 2, 2) & A_CHARTEXT) == ' ');
  assert((mvwinch(window, 3, 2) & A_CHARTEXT) == ' ');
  werase(window);
  nvtop_line_plot(window, 4, samples, 0, false, legend);
  assert((mvwinch(window, 3, 0) & A_CHARTEXT) == ' ');
  delwin(window);
  endwin();
  delscreen(screen);
  fclose(input);
  fclose(output);
  puts("UI history: missing samples and gaps passed");
  return 0;
}
