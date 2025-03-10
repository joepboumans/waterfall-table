/*
    Put copyright here
*/

#include "waterfall.hpp"
#include <bf_rt/bf_rt.hpp>
#include <cstdio>
#include <cstdlib>
#include <dvm/bf_drv_intf.h>
#include <getopt.h>
#include <target-utils/clish/thread.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  printf("Start running Control plane\n");
  Waterfall Waterfall;
  Waterfall.run();
  printf("Finished running!\n");
}
