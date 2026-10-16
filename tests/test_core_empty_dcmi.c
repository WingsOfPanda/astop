/* Interpose enumeration only, so the CLI's empty-device path is testable. */
int dcmiv2_init(void) { return -1; }
int dcmi_init(void) { return 0; }
int dcmi_get_card_list(int *count, int *cards, int length) {
  (void)cards;
  (void)length;
  *count = 0;
  return 0;
}
