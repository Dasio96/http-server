#include "netstack.h"
#include <stdio.h>
#include <unistd.h>

int main(void) {
  char dev_name[16] = "tap0";
  int tap_fd = tap_alloc(dev_name);

  if (tap_fd < 0) {
    fprintf(stderr, "Failed to allocate TAP device\n");
    return 1;
  }

  printf("Successfully created TAP interface: %s\n", dev_name);
  printf("Listening for incoming frames...\n");

  while (1) {
    handle_ethernet_frame(tap_fd);
  }

  close(tap_fd);
  return 0;
}
