#include "netstack.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <linux/if_tun.h>
#include <net/if.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int tap_alloc(char *dev) {
  struct ifreq ifr;
  int fd, err;

  if ((fd = open("/dev/net/tun", O_RDWR)) < 0) {
    perror("error opening /dev/net/tun");
    return fd;
  }

  memset(&ifr, 0, sizeof(ifr));

  ifr.ifr_flags = IFF_TAP | IFF_NO_PI;

  if (*dev) {
    strncpy(ifr.ifr_name, dev, IFNAMSIZ);
  }

  if ((err = ioctl(fd, TUNSETIFF, (void *)&ifr)) < 0) {
    perror("error TUNSETIFF");
    close(fd);
    return err;
  }

  strcpy(dev, ifr.ifr_name);
  return fd;
}

void handle_ethernet_frame(int tap_fd) {
  char buffer[1514];
  ssize_t bytes_read = read(tap_fd, buffer, sizeof(buffer));

  if (bytes_read <= 0) {
    perror("error reading from TAP device");
    return;
  }

  struct eth_hdr *hdr = (struct eth_hdr *)buffer;
  uint16_t ethertype = ntohs(hdr->ethertype);

  if (ethertype == ETH_P_ARP) {
    struct arp_hdr *arp = (struct arp_hdr *)(buffer + sizeof(struct eth_hdr));

    if (ntohs(arp->opcode) == ARP_REQUEST) {
      printf("received ARP request\n");

      uint8_t my_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
      uint32_t my_ip = inet_addr("192.168.1.1");

      arp->opcode = htons(ARP_REPLY);

      memcpy(arp->target_mac, arp->sender_mac, 6);
      arp->target_ip = arp->sender_ip;

      memcpy(arp->sender_mac, my_mac, 6);
      arp->sender_ip = my_ip;

      memcpy(hdr->dmac, hdr->smac, 6);
      memcpy(hdr->smac, my_mac, 6);

      write(tap_fd, buffer, bytes_read);
      printf("Send arp reply!\n");
    }
  }
}
