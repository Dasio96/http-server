#ifndef NETSTACK_H
#define NETSTACK_H

#include <stdint.h>

#define ETH_ALEN 6

int tap_alloc(char *dev);
void handle_ethernet_frame(int tap_fd);

struct eth_hdr {
  uint8_t dmac[ETH_ALEN];
  uint8_t smac[ETH_ALEN];
  uint16_t ethertype;
} __attribute__((packed));

#endif // !NETSTACK_H
