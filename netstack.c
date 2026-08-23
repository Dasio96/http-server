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

  printf("received frame: %zd bytes, ethertype: 0x%04x\n", bytes_read,
         ethertype);
}
