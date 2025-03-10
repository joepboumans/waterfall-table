/*
    Put copyright here
*/

#include "simple_digest.hpp"
#include <bf_rt/bf_rt.hpp>
#include <cstdio>
#include <cstdlib>
#include <dvm/bf_drv_intf.h>
#include <getopt.h>
#include <target-utils/clish/thread.h>
#include <unistd.h>

namespace {
constexpr uint32_t QUEUE_SHAPING_RATE_DEFAULT_10Gbps = 10000000;
constexpr uint32_t QUEUE_SHAPING_RATE_DEFAULT_1Gbps = 1000000;
constexpr uint32_t QUEUE_SHAPING_RATE_DEFAULT_100Mbps = 100000;
constexpr uint32_t QUEUE_SHAPING_RATE_DEFAULT_10Mbps = 10000;
constexpr uint32_t QUEUE_SHAPING_RATE_DEFAULT_100Kbps = 100;

constexpr uint32_t PARAM_SCALING_DEFAULT =
    20; // When using larger size packets (>500), this should be at least 6~10
constexpr uint32_t QUEUE_SHAPING_RATE_DEFAULT =
    QUEUE_SHAPING_RATE_DEFAULT_1Gbps;
} // namespace

int main(int argc, char *argv[]) {
  printf("Start running Control plane\n");
  SimpleDigest SimpleDigest;
  SimpleDigest.run();
  printf("Finished running!\n");
}
