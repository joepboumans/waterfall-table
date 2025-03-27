/*
    Put copyright here
*/

#include "common.h"
#include "pcap_reader.hpp"
#include "waterfall.hpp"
#include <bf_rt/bf_rt.hpp>
#include <cstdio>
#include <cstdlib>
#include <dvm/bf_drv_intf.h>
#include <getopt.h>
#include <iostream>
#include <target-utils/clish/thread.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  option longopts[] = {{"dataset", required_argument, NULL, 'd'},
                       {"real", optional_argument, NULL, 'r'}};
  bool real = false;
  std::string filePath = "/workspace/PDS-Simulator/data/32_test.pcap";
  while (1) {
    const int opt = getopt_long(argc, argv, "d:r", longopts, 0);
    if (opt == -1) {
      break;
    }

    switch (opt) {
    case 'd':
      filePath = optarg;
      std::cout << "Input dataset: " << filePath << std::endl;
      break;
    case 'r':
      real = true;
      std::cout << "Set to run on real hardware" << std::endl;
      break;
    }
  }

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
