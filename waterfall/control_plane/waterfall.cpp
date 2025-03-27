#ifndef _WATERFALL_CPP
#define _WATERFALL_CPP

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

using namespace std;

Waterfall::Waterfall(TupleSize sz, bool real)
    : ControlPlane("waterfall"), mTupleSz(sz) {

  array<uint32_t, 2> ports = {0, 0};
  if (real) {
    ports = {132, 140};
  } else {
    ports = {0, 1};
  }

  /*const auto forwardTable =
   * ControlPlane::getTable("WaterfallIngress.forward");*/
  /*ControlPlane::addEntry(forwardTable, {{"ig_intr_md.ingress_port",
   * ports[0]}},*/
  /*                       {{"dst_port", ports[1]}}, "WaterfallIngress.hit");*/
  /*ControlPlane::addEntry(forwardTable, {{"ig_intr_md.ingress_port",
   * ports[1]}},*/
  /*                       {{"dst_port", ports[0]}}, "SwitchIngress.hit");*/

  for (auto &port : ports) {

    bf_pal_front_port_handle_t port_handle;
    bf_status_t bf_status =
        bf_pm_port_dev_port_to_front_panel_port_get(0, port, &port_handle);
    bf_status =
        bf_pm_port_add(0, &port_handle, BF_SPEED_100G, BF_FEC_TYP_REED_SOLOMON);
    bf_pm_port_enable(0, &port_handle);

    if (bf_status != BF_SUCCESS) {
      printf("Error: %s\n", bf_err_str(bf_status));
      throw runtime_error("Failed to add entry");
    }
    std::cout << "Added port " << port << " to pm" << std::endl;
  }

  uint32_t nTables = 3;
  std::cout << "Start setting up swap and tables..." << std::endl;
  vector<string> loc = {"_hi", "_lo"};
  vector<vector<string>> tableNames(2);
  for (size_t l = 0; l <= 1; l++) {
    for (size_t x = 1; x <= nTables; x++) {
      string name = "table_" + to_string(x) + loc[l];
      tableNames[l].push_back(name);
    }
    mTablesVec.push_back(Waterfall::getTableList(tableNames[l]));
  }
  vector<vector<string>> swapNames(2);
  for (size_t l = 0; l <= 1; l++) {
    for (size_t x = 1; x <= nTables; x++) {
      string name = "swap" + to_string(x) + loc[l];
      swapNames[l].push_back(name);
    }
    mSwapVec.push_back(Waterfall::getTableList(swapNames[l]));
  }
  std::cout << "...got swap and tables" << std::endl;
  auto resubTable = Waterfall::getTable("resub");

  std::cout << "Start adding all entries to the swaps and resub..."
            << std::endl;

  for (size_t l = 0; l <= 1; l++) {
    string currLookup = "WaterfallIngress.lookup" + to_string(1) + loc[l];
    ControlPlane::addEntry(mSwapVec[l][0], {{"ig_intr_md.resubmit_flag", 0}},
                           currLookup);
    string currDoSwap = "WaterfallIngress.do_swap" + to_string(1) + loc[l];
    ControlPlane::addEntry(mSwapVec[l][0], {{"ig_intr_md.resubmit_flag", 1}},
                           currDoSwap);
  }

  for (size_t i = 0; i <= nTables; i++) {
    for (size_t j = 0; j <= nTables; j++) {
      // If the found indexes are equal and a match has been found
      if (j == i and j > 0 and i > 0) {
        ControlPlane::addEntry(resubTable,
                               {{"ig_md.found_hi", i}, {"ig_md.found_lo", j}},
                               "WaterfallIngress.no_resubmit");

        for (size_t x = 2; x <= nTables; x++) {
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

      for (size_t x = 2; x <= nTables; x++) {
        for (size_t l = 0; l <= 1; l++) {
          string currLookup = "WaterfallIngress.lookup" + to_string(x) + loc[l];
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
  for (size_t x = 2; x <= nTables; x++) {
    for (size_t l = 0; l <= 1; l++) {
      string currDoSwap = "WaterfallIngress.do_swap" + to_string(x) + loc[l];
      ControlPlane::addEntry(mSwapVec[l][x - 1],
                             {
                                 {"ig_intr_md.resubmit_flag", 1},
                             },
                             currDoSwap);
    }
  }
  ControlPlane::addEntry(resubTable,
                         {
                             {"ig_intr_md.resubmit_flag", 1},
                         },
                         "WaterfallIngress.do_digest");
  std::cout << "... added all entries succesfully" << std::endl;
}

// Returns a list of len tables which all share the same name
vector<shared_ptr<const bfrt::BfRtTable>>
Waterfall::getTableList(vector<string> names) {
  vector<shared_ptr<const bfrt::BfRtTable>> vec;
  for (const auto &name : names) {
    auto table = ControlPlane::getTable(name);
    if (table == NULL) {
      std::cout << "Table " << name << " could not be found!" << std::endl;
      throw runtime_error("Could not find table in BfRt");
    }
    vec.push_back(table);
  }
  return vec;
}

// Returns hash value of Waterfall or FCM Sketch table
// h indicates which table or depth
uint32_t Waterfall::hashing(const uint8_t *nums, size_t sz, uint32_t h) {
  uint32_t crc = 0;
  switch (h) {
  case 3:
    crc = 0xFFF00000;
    break;
  case 2:
    crc = 0xFF000000;
    break;
  case 1:
    crc = 0xF0000000;
    break;
  case 0:
    crc = 0x00000000;
    break;
  }
  crc = crc32(crc, nums, sz);
  return crc;
}

void Waterfall::run() {
  const auto digest = ControlPlane::getLearnFilter("digest");
  printf("Learnfilter is setup, can now start to receive data...\n");

  auto firstReceivedTime = std::chrono::high_resolution_clock::now();
  auto lastReceivedTime = std::chrono::high_resolution_clock::now();
  bool hasReceivedFirstDigest = false;

  while (true) {
    if (ControlPlane::mLearnInterface.hasNewData) {
      if (!hasReceivedFirstDigest) {
        firstReceivedTime = std::chrono::high_resolution_clock::now();
        hasReceivedFirstDigest = true;
        std::cout << "First data received from digest" << std::endl;
      }

      ControlPlane::mLearnInterface.hasNewData = false;
      lastReceivedTime = std::chrono::high_resolution_clock::now();
    }

    auto time = std::chrono::high_resolution_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(time -
                                                              lastReceivedTime)
                .count() >= 5000 and
        ControlPlane::mLearnInterface.mLearnDataVec.size() > 2) {
      std::cout << "Have no received any digest for over 1s, quiting run loop"
                << std::endl;

      auto totalTime = std::chrono::duration_cast<std::chrono::milliseconds>(
          lastReceivedTime - firstReceivedTime);
      std::cout << "Took a total of " << totalTime.count()
                << " ms to receive all digests" << std::endl;

      break;
    }
  }
  usleep(10);
}

void Waterfall::collectFromDataPlane() {
  std::cout << "Recieved data from digest "
            << ControlPlane::mLearnInterface.mLearnDataVec.size()
            << " total packets" << std::endl;

  // Collect data from Waterfall
  for (const auto &x : ControlPlane::mLearnInterface.mLearnDataVec) {
    vector<uint8_t> src_addr(4);
    memcpy(src_addr.data(), &x, 4);
    std::reverse(src_addr.begin(), src_addr.end());
    TUPLE tup(src_addr.data(), mTupleSz);
    mUniqueTuples.insert(tup);
  }
  std::cout << "Found " << mUniqueTuples.size() << " unique tuples"
            << std::endl;
}

void Waterfall::verify(vector<TUPLE> inTuples) {
  for (auto &tup : inTuples) {
    mUniqueInTuples.insert(tup);
  }
  printf("[WaterfallFcm - verify] Calculate Waterfall F1-score...\n");
  uint32_t true_pos = 0;
  uint32_t false_pos = 0;
  uint32_t true_neg = 0;
  uint32_t false_neg = 0;

  // Compare dataset tuples with Waterfall Tuples
  printf("False positives (Halucinations?):\n");
  for (auto &tup : mUniqueTuples) {
    if (mUniqueInTuples.find(tup) != mUniqueInTuples.end()) {
      true_pos++;
    } else {
      std::cout << tup << std::endl;
      // Ignore local network tuples
      if (tup.num_array[0] == 192 and tup.num_array[1] == 168) {
        continue;
      }
      false_pos++;
    }
  }

  printf("False negatives (Not seen by Waterfall):\n");
  for (auto &tup : mUniqueInTuples) {
    if (mUniqueTuples.find(tup) != mUniqueTuples.end()) {
      continue;
    } else {
      false_neg++;
      std::cout << tup << " : " << std::endl;
      vector<string> loc = {"_hi", "_lo"};
      for (size_t currLoc = 0; currLoc < mTablesVec.size(); currLoc++) {
        for (size_t t = 0; t < mTablesVec[currLoc].size(); t++) {
          uint32_t idx = hashing(tup.num_array, 4, t) % WATERFALL_WIDTH;
          uint16_t val = ControlPlane::getEntry(mTablesVec[currLoc][t], idx);
          std::cout << "Table " << currLoc << " at idx " << idx << " : "
                    << int(uint8_t(val >> 8)) << "." << int(uint8_t(val))
                    << std::endl;
        }
      }
    }
  }

  // F1 Score - Accuracy of detect flows
  double recall = 0.0;
  double precision = 0.0;
  double f1 = 0.0;
  precision = (double)true_pos / (true_pos + false_pos);
  recall = (double)true_pos / (true_pos + false_neg);
  f1 = 2 * ((recall * precision) / (precision + recall));
  printf("[WaterfallFcm - verify] recall = %.5f precision = %.5f f1 = %.5f\n",
         recall, precision, f1);

  double load_factor = (double)mUniqueInTuples.size() / mUniqueTuples.size();
  printf("([WaterfallFcm - verify] Load factor : %f\tUnique Tuples : %zu \n",
         load_factor, mUniqueTuples.size());

  size_t learnDataVecSize = ControlPlane::mLearnInterface.mLearnDataVec.size();
  double total_lf = (double)learnDataVecSize / mUniqueInTuples.size();
  printf("[WaterfallFcm - verify] Total load factor : %f\tTotal received "
         "tuples %zu\n",
         total_lf, learnDataVecSize);

  // Cardinality - Number of seen unique flows
  std::cout << "[Waterfall] Cardinality : " << mUniqueTuples.size()
            << std::endl;
}

#endif
