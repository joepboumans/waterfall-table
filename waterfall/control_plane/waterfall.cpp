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

  std::cout << "Start adding Swap entries..." << std::endl;

  mTablesVec = Waterfall::getTableList("table_", 4);
  mSwapVec = Waterfall::getTableList("swap", 4);
  auto resubTable = Waterfall::getTable("resub");

  std::cout << "...got all tables" << std::endl;
  std::cout << "Start adding all entries to the swaps and resub..."
            << std::endl;

  std::vector<std::string> loc = {"_hi", "_lo"};
  uint32_t idx = 0;
  for (const auto &currLoc : loc) {
    std::string currLookup =
        "WaterfallIngress.lookup" + std::to_string(1) + currLoc;
    ControlPlane::addEntry(mSwapVec[idx], {{"ig_intr_md.resubmit_flag", 0}},
                           currLookup);
    std::string currDoSwap =
        "WaterfallIngress.do_swap" + std::to_string(1) + currLoc;
    ControlPlane::addEntry(mSwapVec[idx], {{"ig_intr_md.resubmit_flag", 1}},
                           currDoSwap);
    idx++;
  }

  for (size_t i = 0; i <= 4; i++) {
    for (size_t j = 0; j <= 4; j++) {
      // If the found indexes are equal and a match has been found
      if (j == i and j > 0 and i > 0) {
        ControlPlane::addEntry(resubTable,
                               {{"ig_md.found_hi", i}, {"ig_md.found_lo", j}},
                               "WaterfallIngress.no_resubmit");

        idx = 2;
        for (size_t x = 2; x <= 4; x++) {
          for (const auto &currLoc : loc) {
            ControlPlane::addEntry(mSwapVec[idx],
                                   {{"ig_intr_md.resubmit_flag", 0},
                                    {"ig_md.found_hi", i},
                                    {"ig_md.found_lo", j}},
                                   "WaterfallIngress.no_action");
            idx++;
          }
        }
        continue;
      }

      // Resubmit if the indexes do not match. Add all mismatching entries to
      // prevent undefined behaviour
      ControlPlane::addEntry(resubTable,
                             {{"ig_md.found_hi", i}, {"ig_md.found_lo", j}},
                             "WaterfallIngress.resubmit_hdr");

      idx = 2;
      for (size_t x = 2; x <= 4; x++) {
        for (const auto &currLoc : loc) {
          std::string currLookup =
              "WaterfallIngress.lookup" + std::to_string(x) + currLoc;
          ControlPlane::addEntry(mSwapVec[idx],
                                 {{"ig_intr_md.resubmit_flag", 0},
                                  {"ig_md.found_hi", i},
                                  {"ig_md.found_lo", j}},
                                 currLookup);
          idx++;
        }
      }
    }
  }

  // If it has been resubmitted then always perform a swap
  idx = 2;
  for (size_t x = 2; x <= 4; x++) {
    for (const auto &currLoc : loc) {
      std::string currDoSwap =
          "WaterfallIngress.do_swap" + std::to_string(x) + currLoc;
      ControlPlane::addEntry(mSwapVec[idx],
                             {
                                 {"ig_intr_md.resubmit_flag", 1},
                             },
                             currDoSwap);
      idx++;
    }
  }
  std::cout << "... added all entries succesfully" << std::endl;
}

// Returns a list of len tables which all share the same name
std::vector<std::shared_ptr<const bfrt::BfRtTable>>
Waterfall::getTableList(std::string name, uint32_t len) {
  std::vector<std::shared_ptr<const bfrt::BfRtTable>> vec;
  for (uint32_t x = 1; x <= len; x++) {
    for (const auto &loc : {"_hi", "_lo"}) {
      std::string currName = name + std::to_string(x) + loc;
      auto table = ControlPlane::getTable(currName);
      if (table == NULL) {
        std::cout << "Table " << currName << " could not be found!"
                  << std::endl;
        throw std::runtime_error("Could not find table in BfRt");
      }
      vec.push_back(table);
    }
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
  for (const auto &x : ControlPlane::mLearnInterface.mLearnDataVec) {
    uniqueSrcAddress.insert(x);
  }
  std::cout << "Found " << uniqueSrcAddress.size() << " unique tupels"
            << std::endl;
  std::cout << "Finished the test exit via ctrl-c"
            << std::endl;
  while (true) {
    sleep(100);
  }
}

#endif
