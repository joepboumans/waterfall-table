#ifndef _SIMPLE_DIGEST_CPP
#define _SIMPLE_DIGEST_CPP

#include "simple_digest.hpp"
#include "ControlPlane.hpp"
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <unistd.h>

extern "C" {
#include <bf_pm/bf_pm_intf.h>
#include <traffic_mgr/traffic_mgr.h>
}

SimpleDigest::SimpleDigest() : ControlPlane("simple_digest") {
  const auto forwardTable = ControlPlane::getTable("SwitchIngress.forward");
  /*ControlPlane::addEntry(forwardTable, {{"ig_intr_md.ingress_port", 0}},*/
  /*                       {{"dst_port", 0}}, "SwitchIngress.hit");*/
  ControlPlane::addEntry(forwardTable, {{"ig_intr_md.ingress_port", 132}},
                         {{"dst_port", 140}}, "SwitchIngress.hit");
  ControlPlane::addEntry(forwardTable, {{"ig_intr_md.ingress_port", 140}},
                         {{"dst_port", 132}}, "SwitchIngress.hit");

  std::array<uint32_t, 2> ports = {
      132,
      140,
  };

  for (auto &port : ports) {
    bf_pal_front_port_handle_t port_handle;
    bf_status_t bf_status =
        bf_pm_port_dev_port_to_front_panel_port_get(0, port, &port_handle);
    bf_status =
        bf_pm_port_add(0, &port_handle, BF_SPEED_100G, BF_FEC_TYP_REED_SOLOMON);
    bf_pm_port_enable(0, &port_handle);

    if (bf_status != BF_SUCCESS) {
      printf("Error: %s\n", bf_err_str(bf_status));
      throw std::runtime_error("Failed to add entry");
    }
    std::cout << "Added port " << port << " to pm" << std::endl;
  }
}

void SimpleDigest::run() {
  const auto digest = ControlPlane::getLearnFilter("digest");
  printf("Setup learnfilter with callback\n");

  auto firstReceivedTime = std::chrono::high_resolution_clock::now();
  auto lastReceivedTime = std::chrono::high_resolution_clock::now();
  bool hasReceivedFirstDigest = false;

  while (true) {
    if (ControlPlane::mLearnInterface.hasNewData) {
      if (!hasReceivedFirstDigest) {
        firstReceivedTime = std::chrono::high_resolution_clock::now();
        hasReceivedFirstDigest = true;
      }

      /*std::cout << "Recieved data from digest "*/
      /*          << ControlPlane::mLearnInterface.mLearnDataVec.size()*/
      /*          << " total packets\r";*/
      ControlPlane::mLearnInterface.hasNewData = false;
      lastReceivedTime = std::chrono::high_resolution_clock::now();
    }

    auto time = std::chrono::high_resolution_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(time -
                                                              lastReceivedTime)
                .count() >= 10000 and
        hasReceivedFirstDigest) {
      std::cout << "Have no received any digest for over 1s, quiting run loop"
                << std::endl;

      auto totalTime = std::chrono::duration_cast<std::chrono::milliseconds>(
          lastReceivedTime - firstReceivedTime);
      std::cout << "Took a total of " << totalTime.count()
                << " ms to receive all digests" << std::endl;

      std::cout << "Recieved data from digest "
                << ControlPlane::mLearnInterface.mLearnDataVec.size()
                << " total packets" << std::endl;

      /*std::cout << "Recieved data from digest" << std::endl;*/
      /*for (const auto &x : ControlPlane::mLearnInterface.mLearnDataVec) {*/
      /*  std::cout << x << " ";*/
      /*}*/
      /*std::cout << std::endl;*/

      break;
    }

    /*usleep(100);*/
  }
  std::set<uint64_t> uniqueSrcAddress;
  for    (const auto &x : ControlPlane::mLearnInterface.mLearnDataVec) {
    uniqueSrcAddress.insert(x);
  }
  std::cout << "Found " << uniqueSrcAddress.size() << " unique tupels" << std::endl;
  std::cout << "Finished the test exit via ctrl-z or keep using the switch cli"
            << std::endl;
  while (true) {
    sleep(100);
  }
}

#endif
