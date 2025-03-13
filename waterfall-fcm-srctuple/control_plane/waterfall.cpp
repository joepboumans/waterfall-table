#ifndef _SIMPLE_DIGEST_CPP
#define _SIMPLE_DIGEST_CPP

#include "waterfall.hpp"
#include "ControlPlane.hpp"
#include "bf_rt/bf_rt_common.h"
#include <bf_rt/bf_rt_table.hpp>
#include <chrono>
#include <cinttypes>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <unordered_map>

extern "C" {
#include <bf_pm/bf_pm_intf.h>
#include <traffic_mgr/traffic_mgr.h>
}

Waterfall::Waterfall() : ControlPlane("waterfall_fcm") {
  std::array<uint32_t, 2> ports = {
      0,
      1,
  };
  const auto forwardTable = ControlPlane::getTable("WaterfallIngress.forward");
  ControlPlane::addEntry(forwardTable, {{"ig_intr_md.ingress_port", ports[0]}},
                         {{"dst_port", ports[1]}}, "WaterfallIngress.hit");
  ControlPlane::addEntry(forwardTable, {{"ig_intr_md.ingress_port", ports[1]}},
                         {{"dst_port", ports[0]}}, "SwitchIngress.hit");

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

  std::cout << "Start setting up swap and tables..." << std::endl;
  std::vector<std::string> loc = {"_hi", "_lo"};
  std::vector<std::vector<std::string>> tableNames(2);
  for (size_t l = 0; l <= 1; l++) {
    for (size_t x = 1; x <= 4; x++) {
      std::string name = "table_" + std::to_string(x) + loc[l];
      tableNames[l].push_back(name);
    }
    mTablesVec.push_back(Waterfall::getTableList(tableNames[l]));
  }
  std::vector<std::vector<std::string>> swapNames(2);
  for (size_t l = 0; l <= 1; l++) {
    for (size_t x = 1; x <= 4; x++) {
      std::string name = "swap" + std::to_string(x) + loc[l];
      swapNames[l].push_back(name);
    }
    mSwapVec.push_back(Waterfall::getTableList(swapNames[l]));
  }
  std::cout << "...got swap and tables" << std::endl;
  auto resubTable = Waterfall::getTable("resub");

  std::cout << "Start adding all entries to the swaps and resub..."
            << std::endl;

  for (size_t l = 0; l <= 1; l++) {
    std::string currLookup =
        "WaterfallIngress.lookup" + std::to_string(1) + loc[l];
    ControlPlane::addEntry(mSwapVec[l][0], {{"ig_intr_md.resubmit_flag", 0}},
                           currLookup);
    std::string currDoSwap =
        "WaterfallIngress.do_swap" + std::to_string(1) + loc[l];
    ControlPlane::addEntry(mSwapVec[l][0], {{"ig_intr_md.resubmit_flag", 1}},
                           currDoSwap);
  }

  for (size_t i = 0; i <= 4; i++) {
    for (size_t j = 0; j <= 4; j++) {
      // If the found indexes are equal and a match has been found
      if (j == i and j > 0 and i > 0) {
        ControlPlane::addEntry(resubTable,
                               {{"ig_md.found_hi", i}, {"ig_md.found_lo", j}},
                               "WaterfallIngress.no_resubmit");

        for (size_t x = 2; x <= 4; x++) {
          for (size_t l = 0; l <= 1; l++) {
            ControlPlane::addEntry(mSwapVec[l][x - 1],
                                   {{"ig_intr_md.resubmit_flag", 0},
                                    {"ig_md.found_hi", i},
                                    {"ig_md.found_lo", j}},
                                   "WaterfallIngress.no_action");
          }
        }
        continue;
      }

      // Resubmit if the indexes do not match. Add all mismatching entries to
      // prevent undefined behaviour
      ControlPlane::addEntry(resubTable,
                             {{"ig_md.found_hi", i}, {"ig_md.found_lo", j}},
                             "WaterfallIngress.resubmit_hdr");

      for (size_t x = 2; x <= 4; x++) {
        for (size_t l = 0; l <= 1; l++) {
          std::string currLookup =
              "WaterfallIngress.lookup" + std::to_string(x) + loc[l];
          ControlPlane::addEntry(mSwapVec[l][x - 1],
                                 {{"ig_intr_md.resubmit_flag", 0},
                                  {"ig_md.found_hi", i},
                                  {"ig_md.found_lo", j}},
                                 currLookup);
        }
      }
    }
  }

  // If it has been resubmitted then always perform a swap
  for (size_t x = 2; x <= 4; x++) {
    for (size_t l = 0; l <= 1; l++) {
      std::string currDoSwap =
          "WaterfallIngress.do_swap" + std::to_string(x) + loc[l];
      ControlPlane::addEntry(mSwapVec[l][x - 1],
                             {
                                 {"ig_intr_md.resubmit_flag", 1},
                             },
                             currDoSwap);
    }
  }
  std::cout << "... added all entries succesfully" << std::endl;

  std::cout << "Start setting up names for Sketch regs" << std::endl;
  mSketchVec.resize(2);
  std::vector<std::vector<std::string>> sketchNames(2);
  for (size_t d = 1; d <= 2; d++) {
    for (size_t l = 1; l <= 3; l++) {
      std::string name =
          "sketch_reg_l" + std::to_string(l) + "_d" + std::to_string(d);
      sketchNames[d - 1].push_back(name);
    }
  }
  std::cout << "Start adding FCM Sketch tables" << std::endl;
  for (size_t d = 0; d <= 1; d++) {
    mSketchVec[d] = Waterfall::getTableList(sketchNames[d]);
  }

  std::cout << "Add pkt count reg" << std::endl;
  mPktCount = ControlPlane::getTable("FcmEgress.num_pkt");

  std::vector<bf_rt_id_t> fieldIds;
  bf_status_t bf_status = mPktCount->keyFieldIdListGet(&fieldIds);
  for (const auto &id : fieldIds) {
    std::cout << id << " ";
  }
  std::cout << std::endl;
  uint32_t pkt_count = 0;
  pkt_count = ControlPlane::getEntry(mPktCount, 0);
  std::cout << "Packet count: " << pkt_count << std::endl;

  /*for (size_t d = 0; d <= 1; d++) {*/
  /*  for (auto &table : mSketchVec[d]) {*/
  /*  }*/
  /*}*/
}

// Returns a list of len tables which all share the same name
std::vector<std::shared_ptr<const bfrt::BfRtTable>>
Waterfall::getTableList(std::vector<std::string> names) {
  std::vector<std::shared_ptr<const bfrt::BfRtTable>> vec;
  for (const auto &name : names) {
    auto table = ControlPlane::getTable(name);
    if (table == NULL) {
      std::cout << "Table " << name << " could not be found!" << std::endl;
      throw std::runtime_error("Could not find table in BfRt");
    }
    vec.push_back(table);
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

      if (ControlPlane::mLearnInterface.mLearnDataVec.size() <= 10000) {
        std::cout << "Recieved data from digest" << std::endl;
        for (const uint32_t &x : ControlPlane::mLearnInterface.mLearnDataVec) {
          uint8_t src_addr[4];
          memcpy(src_addr, &x, 4);
          // Skip local messages
          if (src_addr[3] == 192 and src_addr[2] == 168) {
            continue;
          }
          for (int i = 3; i >= 0; i--) {
            std::cout << int(src_addr[i]);
            if (i > 0) {
              std::cout << ".";
            }
          }
          std::cout << " ";
        }
        std::cout << std::endl;
      }

      break;
    }

    /*usleep(100);*/
  }
  std::set<uint64_t> uniqueSrcAddress;
  for (const auto &x : ControlPlane::mLearnInterface.mLearnDataVec) {
    uint8_t src_addr[4];
    memcpy(src_addr, &x, 4);
    // Skip local messages
    if (src_addr[3] == 192 and src_addr[2] == 168) {
      continue;
    }
    uniqueSrcAddress.insert(x);
  }
  std::cout << "Found " << uniqueSrcAddress.size() << " unique tupels"
            << std::endl;

  uint32_t pkt_count = ControlPlane::getEntry(mPktCount, 0);
  std::cout << "Package count :" << pkt_count << std::endl;
  pkt_count = ControlPlane::getAllEntries(mSketchVec[0][0]);
  std::cout << "packet count: " << pkt_count << std::endl;
  std::cout << "Finished the test exit via ctrl-c" << std::endl;
  while (true) {
    sleep(100);
  }
}

#endif
