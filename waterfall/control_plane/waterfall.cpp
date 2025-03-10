#ifndef _SIMPLE_DIGEST_CPP
#define _SIMPLE_DIGEST_CPP

#include "waterfall.hpp"
#include "ControlPlane.hpp"
#include <bf_rt/bf_rt_table.hpp>
#include <chrono>
#include <cinttypes>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <unistd.h>

extern "C" {
#include <bf_pm/bf_pm_intf.h>
#include <traffic_mgr/traffic_mgr.h>
}

Waterfall::Waterfall() : ControlPlane("waterfall") {
  /*const auto forwardTable = ControlPlane::getTable("SwitchIngress.forward");*/
  /*ControlPlane::addEntry(forwardTable, {{"ig_intr_md.ingress_port", 132}},*/
  /*                       {{"dst_port", 140}}, "SwitchIngress.hit");*/
  /*ControlPlane::addEntry(forwardTable, {{"ig_intr_md.ingress_port", 140}},*/
  /*                       {{"dst_port", 132}}, "SwitchIngress.hit");*/
  /**/
  std::array<uint32_t, 2> ports = {
      0,
      1,
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

  auto resubTable = Waterfall::getTable("resub");
  ControlPlane::addEntry(resubTable, {{"ig_md.found", true}},
                         "SwitchIngress.no_action");
  ControlPlane::addEntry(resubTable, {{"ig_md.found", false}},
                         "SwitchIngress.resubmit_hdr");

  std::cout << "Start adding Swap entries" << std::endl;

  mTablesVec = Waterfall::getTableList("table_", 4);
  mSwapVec = Waterfall::getTableList("swap", 4);

  ControlPlane::addEntry(mSwapVec[0], {{"ig_intr_md.resubmit_flag", 0}},
                         "SwitchIngress.lookup1");
  ControlPlane::addEntry(mSwapVec[0], {{"ig_intr_md.resubmit_flag", 1}},
                         "SwitchIngress.do_swap1");
  for (size_t i = 2; i < 5; i++) {
    std::string currLookup = "SwitchIngress.lookup" + std::to_string(i);

    ControlPlane::addEntry(mSwapVec[i - 1], {{"ig_intr_md.resubmit_flag", 0}},
                           currLookup);

    std::string currDoSwap = "SwitchIngress.do_swap" + std::to_string(i);
    ControlPlane::addEntry(mSwapVec[i - 1], {{"ig_intr_md.resubmit_flag", 1}},
                           currDoSwap);
  }
}

// Returns a list of len tables which all share the same name
std::vector<std::shared_ptr<const bfrt::BfRtTable>>
Waterfall::getTableList(std::string name, uint32_t len) {
  std::vector<std::shared_ptr<const bfrt::BfRtTable>> vec(len);
  for (uint32_t x = 1; x < len + 1; x++) {
    std::string currName = name + std::to_string(x);
    vec[x - 1] = ControlPlane::getTable(currName);
  }
  return vec;
}

void Waterfall::run() {
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

      std::cout << "Recieved data from digest" << std::endl;
      for (const auto &x : ControlPlane::mLearnInterface.mLearnDataVec) {
        std::cout << x << " ";
      }
      std::cout << std::endl;

      break;
    }

    /*usleep(100);*/
  }
  std::set<uint64_t> uniqueSrcAddress;
  for (const auto &x : ControlPlane::mLearnInterface.mLearnDataVec) {
    uniqueSrcAddress.insert(x);
  }
  std::cout << "Found " << uniqueSrcAddress.size() << " unique tupels"
            << std::endl;
  std::cout << "Finished the test exit via ctrl-z or keep using the switch cli"
            << std::endl;
  while (true) {
    sleep(100);
  }
}

#endif
