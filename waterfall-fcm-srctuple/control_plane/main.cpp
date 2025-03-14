
#include "control_plane/common.h"
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
  pcapReader dataReader(filePath, TupleSize::SrcTuple);

  printf("Start running Control plane\n");
  Waterfall Waterfall(TupleSize::SrcTuple);
  Waterfall.run();
  Waterfall.verify(dataReader.mTuples);

  printf("Finished running!\n");
  std::cout << "Finished the test exit via ctrl-c" << std::endl;
  while (true) {
    sleep(100);
  }
}
