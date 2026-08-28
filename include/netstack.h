#ifndef NETSTACK_H
#define NETSTACK_H

#include <arpa/inet.h>
#include <net/if.h>
#include <stdint.h>

#define ETH_ALEN 6
#define ETH_P_ARP 0x0806
#define ETH_P_IP 0x0800
#define ARP_REQUEST 1
#define ARP_REPLY 2

struct eth_hdr {
  uint8_t dmac[ETH_ALEN];
  uint8_t smac[ETH_ALEN];
  uint16_t ethertype;
} __attribute__((packed));

struct arp_hdr {
  uint16_t htype;
  uint16_t ptype;
  uint8_t hlen;
  uint8_t plen;
  uint16_t opcode;

  uint8_t sender_mac[6];
  uint32_t sender_ip;
  uint8_t target_mac[6];
  uint32_t target_ip;
} __attribute__((packed));

struct ip_hdr {
  uint8_t ver_ihl;
  uint8_t tos;
  uint16_t tot_len;
  uint16_t id;
  uint16_t flags_frag;
  uint8_t ttl;
  uint8_t protocol;
  uint16_t checksum;
  uint32_t saddr;
  uint32_t daddr;
} __attribute__((packed));

struct icmp_hdr {
  uint8_t type;
  uint8_t code;
  uint16_t checksum;
  uint16_t id;
  uint16_t seq;
} __attribute__((packed));

uint16_t checksum(void *b, int len);
int tap_alloc(char *dev);
void handle_ethernet_frame(int tap_fd);

#endif // !NETSTACK_H
