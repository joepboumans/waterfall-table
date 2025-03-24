
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
  bool real = false;
  if (argc > 1) {
    std::cout << "Running for real hardware" << std::endl;
    real = true;
  }
  std::string filePath = "/workspace/PDS-Simulator/data/32_test.pcap";
  printf("Loading in data set\n");
  pcapReader dataReader(filePath, TupleSize::SrcTuple);

  printf("Start running Control plane\n");
  Waterfall Waterfall(TupleSize::SrcTuple, real);
  Waterfall.run();
  Waterfall.collectFromDataPlane();
  Waterfall.verify(dataReader.mTuples);

  printf("Finished running!\n");
  std::cout << "Finished the test exit via ctrl-c" << std::endl;
  while (true) {
    sleep(100);
  }
}
