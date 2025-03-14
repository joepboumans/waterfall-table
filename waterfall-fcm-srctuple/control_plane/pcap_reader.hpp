#include "common.h"
#include <arpa/inet.h> // For network-related functions
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <glob.h>
#include <iostream>
#include <net/ethernet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <ostream>
#include <pcap.h>
#include <pcap/pcap.h>
#include <stdexcept>
#include <vector>

using std::string;
using std::vector;

class pcapReader {
public:
  vector<TUPLE> mTuples;
  pcapReader(std::string filePath) {
    char errbuf[PCAP_ERRBUF_SIZE];

    std::cout << "Opening file " << filePath << std::endl;
    pcap_t *pcapFile = pcap_open_offline(filePath.data(), errbuf);
    if (pcapFile == nullptr) {
      std::cerr << "Error opening file " << filePath << " : " << errbuf
                << std::endl;
      throw std::runtime_error("Failed to open file");
    }

    // Process packets
    int packetCount = 0;
    while (true) {
      const u_char *packet;
      pcap_pkthdr *pcapHdr;
      int retStatus = pcap_next_ex(pcapFile, &pcapHdr, &packet);
      if (retStatus != 1) {
        printf("Error: %s\n", pcap_geterr(pcapFile));
        break;
      }

      // For a raw IP file, the first nibble of the first byte indicates the IP
      // version.
      u_char ipVersion = packet[0] >> 4;
      if (ipVersion == 6) {
        continue;
      }

      const struct ip *ipHeader = (struct ip *)(packet);
      int protocol = ipHeader->ip_p;

      char srcIP[INET_ADDRSTRLEN];
      char dstIP[INET_ADDRSTRLEN];
      inet_ntop(AF_INET, &(ipHeader->ip_src), srcIP, INET_ADDRSTRLEN);
      inet_ntop(AF_INET, &(ipHeader->ip_dst), dstIP, INET_ADDRSTRLEN);

      std::cout << "IPv4 Packet ";
      std::cout << "Source IP: " << srcIP << " ";
      std::cout << "Destination IP: " << dstIP << " ";
      std::cout << "Protocol: "
                << (protocol == IPPROTO_TCP
                        ? "TCP"
                        : (protocol == IPPROTO_UDP ? "UDP" : "Other"))
                << std::endl;

      TUPLE tup;
      memcpy(&tup.num_array[0], &ipHeader->ip_src, sizeof(ipHeader->ip_src));
      memcpy(&tup.num_array[4], &ipHeader->ip_dst, sizeof(ipHeader->ip_dst));
      tup.num_array[12] = protocol;

      int ipHeaderLength = ipHeader->ip_hl * 4; // ip_hl is in 4-byte words
      const u_char *transportHeader =
          packet + sizeof(struct ether_header) + ipHeaderLength;
      // Extract source and destination ports based on the protocol
      if (protocol == IPPROTO_TCP) {
        const struct tcphdr *tcpHeader = (struct tcphdr *)transportHeader;
        memcpy(&tup.num_array[8], &tcpHeader->th_sport,
               sizeof(tcpHeader->th_sport));
        memcpy(&tup.num_array[10], &tcpHeader->th_dport,
               sizeof(tcpHeader->th_dport));
      } else if (protocol == IPPROTO_UDP) {
        const struct udphdr *udpHeader = (struct udphdr *)transportHeader;
        memcpy(&tup.num_array[8], &udpHeader->uh_sport,
               sizeof(udpHeader->uh_sport));
        memcpy(&tup.num_array[10], &udpHeader->uh_dport,
               sizeof(udpHeader->uh_dport));
      } else {
        continue;
      }

      packetCount++;
      mTuples.push_back(tup);
    }

    std::cout << "Finished dataset with " << packetCount << " packets"
              << std::endl;
    pcap_close(pcapFile);
  }
};
