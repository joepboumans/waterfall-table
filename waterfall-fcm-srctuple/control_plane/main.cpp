
#include "pcap_reader.hpp"
#include "waterfall.hpp"
#include <bf_rt/bf_rt.hpp>
#include <cstdio>
#include <cstdlib>
#include <dvm/bf_drv_intf.h>
#include <getopt.h>
#include <target-utils/clish/thread.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  std::string filePath =
      "/workspace/PDS-Simulator/data/equinix-chicago.20160121-130000.UTC.pcap";
  printf("Loading in data set\n");
  pcapReader dataReader(filePath);
  exit(0);

  printf("Start running Control plane\n");
  Waterfall Waterfall;
  Waterfall.run();

  printf("Finished running!\n");
}
