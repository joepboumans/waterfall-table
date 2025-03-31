#include "waterfall.hpp"
#include "ControlPlane.hpp"
#include "EM_WFCM.hpp"
#include "bf_rt/bf_rt_common.h"
#include "common.h"
#include "ptf/EM/common.h"
#include "zlib.h"
#include <algorithm>
#include <bf_rt/bf_rt_table.hpp>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
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

using namespace std;
using namespace bfrt;

Waterfall::Waterfall(TupleSize sz, bool real)
    : ControlPlane("waterfall_fcm"), mTupleSz(sz) {

  array<uint32_t, 2> ports = {0, 0};
  if (real) {
    ports = {132, 140};
  } else {
    ports = {0, 1};
  }

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
      throw runtime_error("Failed to add entry");
    }
    std::cout << "Added port " << port << " to pm" << std::endl;
  }

  std::cout << "Start setting up swap and tables..." << std::endl;
  vector<string> loc = {"_hi", "_lo"};
  vector<vector<string>> tableNames(2);
  for (size_t l = 0; l <= 1; l++) {
    for (size_t x = 1; x <= 4; x++) {
      string name = "table_" + to_string(x) + loc[l];
      tableNames[l].push_back(name);
    }
    mTablesVec.push_back(Waterfall::getTableList(tableNames[l]));
  }
  vector<vector<string>> swapNames(2);
  for (size_t l = 0; l <= 1; l++) {
    for (size_t x = 1; x <= 4; x++) {
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
  for (size_t x = 2; x <= 4; x++) {
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

  std::cout << "Start setting up names for Sketch regs" << std::endl;
  mSketchVec.resize(2);
  vector<vector<string>> sketchNames(2);
  for (size_t d = 1; d <= 2; d++) {
    for (size_t l = 1; l <= 3; l++) {
      string name = "sketch_reg_l" + to_string(l) + "_d" + to_string(d);
      sketchNames[d - 1].push_back(name);
    }
  }
  std::cout << "Start adding FCM Sketch tables" << std::endl;
  for (size_t d = 0; d <= 1; d++) {
    mSketchVec[d] = Waterfall::getTableList(sketchNames[d]);
  }

  std::cout << "Get num_pkt register" << std::endl;
  mPktCount = ControlPlane::getTable("FcmEgress.num_pkt");
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
        hasReceivedFirstDigest) {
      std::cout << "Have no received any digest for over 5s, quiting run loop"
                << std::endl;

      auto totalTime = std::chrono::duration_cast<std::chrono::milliseconds>(
          lastReceivedTime - firstReceivedTime);
      std::cout << "Took a total of " << totalTime.count()
                << " ms to receive all digests" << std::endl;

      break;
    }
    sleep(1);
  }
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
  std::cout << "Found " << mUniqueTuples.size() << " unique tupels"
            << std::endl;

  // Get pkt count from FCM Sketch
  uint32_t pkt_count = ControlPlane::getEntry(mPktCount, 0);
  std::cout << "Package count : " << pkt_count << std::endl;

  // Collect data from FCM Sketch with indexes from Waterfall
  mSketchData.resize(DEPTH);
  vector<uint32_t> sketchLengths = {W1, W2, W3};
  for (size_t d = 0; d < DEPTH; d++) {
    mSketchData[d].resize(NUM_STAGES);
    for (size_t l = 0; l < NUM_STAGES; l++) {
      mSketchData[d][l].resize(sketchLengths[l]);
    }
  }

  std::cout << "Start collecting sketch data from data plane..." << std::endl;
  for (const auto &srcAddr : mUniqueTuples) {
    for (size_t d = 0; d < DEPTH; d++) {
      uint32_t idx = hashing(srcAddr.num_array, 4, d) % W1;
      for (size_t l = 0; l < NUM_STAGES; l++) {
        uint64_t val = getEntry(mSketchVec[d][l], idx);
        mSketchData[d][l][idx] = val;
        idx = idx / 8;

        if (val <= 0) {
          if (l == 0) {
            std::cout << "d" << d << " l" << l << " at idx " << idx << " : "
                      << val << std::endl;
            throw runtime_error("First counter layer is emtpy for idx, should "
                                "at least containt 1");
          }
          continue;
        }
      }
    }
  }
}

void Waterfall::verify(vector<TUPLE> inTuples) {
  for (auto &tup : inTuples) {
    mUniqueInTuples.insert(tup);
  }
  printf("[WaterfallFcm - verify] Calculate Waterfall F1-score...");
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
      // Ignore local messages and delete it from the set
      if (tup.num_array[0] == 192 and tup.num_array[1] == 168) {
        mUniqueTuples.erase(tup);
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
      std::cout << tup << " : ";
      for (size_t d = 0; d < DEPTH; d++) {
        uint32_t idx = hashing(tup.num_array, 4, d) % W1;
        uint64_t val = ControlPlane::getEntry(mSketchVec[d][0], idx);
        std::cout << "d" << d << " at idx " << idx << " : " << val << std::endl;
      }
      for (size_t loc = 0; loc < mTablesVec.size(); loc++) {
        for (size_t t = 0; t < mTablesVec[loc].size(); t++) {
          uint32_t idx = hashing(tup.num_array, 4, t) % WATERFALL_WIDTH;
          uint16_t val = ControlPlane::getEntry(mTablesVec[loc][t], idx);
          std::cout << "Table " << loc << " at idx " << idx << " : " << val
                    << std::endl;

          std::cout << "Table " << loc << " at idx " << idx << " : "
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

  double load_factor = (double)mUniqueTuples.size() / mUniqueInTuples.size();
  printf("([WaterfallFcm - verify] Load factor : %f\tUnique Tuples : %zu \n",
         load_factor, mUniqueTuples.size());

  size_t learnDataVecSize = ControlPlane::mLearnInterface.mLearnDataVec.size();
  double total_lf = (double)learnDataVecSize / mUniqueInTuples.size();
  printf("[WaterfallFcm - verify] Total load factor : %f\tTotal received "
         "tuples %zu\n",
         total_lf, learnDataVecSize);

  if (precision != 1.0 or recall != 1.0) {
    std::cerr << "Could not find all tuples!" << std::endl;
    std::cerr << "Precision : " << precision << " Recall : " << recall
              << std::endl;
    throw runtime_error("Error in parsing tuples from digest");
  }

  // Cardinality - Number of seen unique flows
  std::cout << "[Waterfall] Cardinality : " << mUniqueTuples.size()
            << std::endl;
  // Wait for 5s to show results
  sleep(5);

  // FSD
  for (auto &tup : inTuples) {
    mTrueCounts[tup]++;
  }
  std::cout << "[Waterfall] Created True Counts, start making True FSD"
            << std::endl;
  using pair_type = decltype(mTrueCounts)::value_type;
  auto maxCountElement =
      std::max_element(mTrueCounts.begin(), mTrueCounts.end(),
                       [](const pair_type &p1, const pair_type &p2) {
                         return p1.second < p2.second;
                       });

  mTrueFSD.resize(maxCountElement->second + 1);
  for (const auto &[tuple, count] : mTrueCounts) {
    mTrueFSD[count]++;
  }

  std::cout << "[Waterfall] Finished True FSD" << std::endl;

  auto start = std::chrono::high_resolution_clock::now();
  calculateFSD();
  auto stop = std::chrono::high_resolution_clock::now();
  auto time =
      std::chrono::duration_cast<std::chrono::milliseconds>(stop - start);

  printf("[EM_FSD] WMRE : %f\n", mWMRE);
  printf("[EM_FSD] Total time %li ms\n", time.count());
}

void Waterfall::calculateFSD() {
  std::cout << "[EM_WFCM] Setting up summary and overflow paths..."
            << std::endl;
  // Summarize counters into single counters
  // depth, stage, idx, {value, total degree, sketch degree}
  vector<vector<vector<vector<uint32_t>>>> summary(DEPTH);
  // depth, degree, value, count
  vector<vector<vector<uint32_t>>> virtualCounters;
  // depth, value, count
  vector<vector<uint32_t>> initFSD(DEPTH);
  // depth, stage, idx, layer, {stage, local degree, total degree, min value}
  vector<vector<vector<vector<vector<uint32_t>>>>> overflowPaths(DEPTH);
  // depth, degree, value, {stage, local colls, total colls, min value}
  vector<vector<vector<vector<vector<uint32_t>>>>> thresholds;
  // depth, degree, value, sketch degree
  vector<vector<vector<uint32_t>>> sketchDegrees;
  vector<uint32_t> maxDegrees = {0, 0};
  uint32_t maxCounterVal = 0;
  vector<vector<uint32_t>> initDegrees = getInitialDegrees();
  vector<uint32_t> stageSzes = {W1, W2, W3};
  vector<uint32_t> counterOverflowVal = {OVERFLOW_LEVEL1, OVERFLOW_LEVEL2};

  // Setup sizes for summary and overflow_paths
  for (size_t d = 0; d < DEPTH; d++) {
    summary[d].resize(NUM_STAGES);
    overflowPaths[d].resize(NUM_STAGES);
    for (size_t stage = 0; stage < NUM_STAGES; stage++) {
      summary[d][stage].resize(stageSzes[stage], vector<uint32_t>(3, 0));
      overflowPaths[d][stage].resize(stageSzes[stage]);
    }
  }
  std::cout << "[EM_WFCM] ...done!" << std::endl;
  std::cout << "[EM_WFCM] Setting up virtual counters and thresholds..."
            << std::endl;

  virtualCounters.resize(DEPTH);
  sketchDegrees.resize(DEPTH);
  thresholds.resize(DEPTH);

  std::cout << "[EM_WFCM] ...done!" << std::endl;
  std::cout << "[EM_WFCM] Load count from sketches into virtual counters and "
               "thresholds..."
            << std::endl;
  for (size_t d = 0; d < DEPTH; d++) {
    for (size_t stage = 0; stage < NUM_STAGES; stage++) {
      for (size_t i = 0; i < stageSzes[stage]; i++) {
        if (mSketchData[d][stage][i] <= 0) {
          continue;
        }

        // Limit value to OVERFLOW_LEVELX values
        summary[d][stage][i][0] = mSketchData[d][stage][i];
        if (mSketchData[d][stage][i] > counterOverflowVal[stage]) {
          summary[d][stage][i][0] = counterOverflowVal[stage];
        }

        // Store local and initial degree
        if (stage == 0) {
          summary[d][stage][i][1] = 1;
          summary[d][stage][i][2] = initDegrees[d][i];
        } else if (stage > 0) { // Start checking childeren from stage 1 and up
          for (size_t k = 0; k < K; k++) {
            uint32_t child_idx = i * K + k;

            // Add childs count, total and sketch degree to current counter
            if (mSketchData[d][stage - 1][child_idx] >
                counterOverflowVal[stage - 1]) {
              summary[d][stage][i][0] += summary[d][stage - 1][child_idx][0];
              summary[d][stage][i][1] += summary[d][stage - 1][child_idx][1];
              summary[d][stage][i][2] += summary[d][stage - 1][child_idx][2];

              // Add overflow path of child to my own path
              for (auto &path : overflowPaths[d][stage - 1][child_idx]) {
                overflowPaths[d][stage][i].push_back(path);
              }
            }
          }
        }

        // If not overflown and non-zero, we are at the end of the path
        if (!(mSketchData[d][stage][i] > counterOverflowVal[stage]) &&
            summary[d][stage][i][0] > 0) {

          uint32_t count = summary[d][stage][i][0];
          uint32_t sketch_degree = summary[d][stage][i][1];
          uint32_t degree = summary[d][stage][i][2];
          maxCounterVal = std::max(maxCounterVal, count);
          maxDegrees[d] = std::max(maxDegrees[d], degree);

          if (maxCounterVal >= initFSD[d].size()) {
            initFSD[d].resize(maxCounterVal + 2);
          }

          if (degree == 1) {
            initFSD[d][count]++;
          } else if (degree == count) {
            initFSD[d][1] += count;
          } else if (degree + 1 == count) {
            initFSD[d][1] += (count - 1);
            initFSD[d][2] += 1;
          } else {
            if (degree >= virtualCounters[d].size()) {
              virtualCounters[d].resize(degree + 1);
              thresholds[d].resize(degree + 1);
              sketchDegrees[d].resize(degree + 1);
            }

            // Separate L1 VC's as these do not require thresholds to solve.
            // Store L1 VC in degree 0
            if (sketch_degree == 1) {
              sketch_degree = degree;
              degree = 1;
            }
            // Add entry to VC with its degree [1] and count [0]
            virtualCounters[d][degree].push_back(count);
            sketchDegrees[d][degree].push_back(sketch_degree);

            thresholds[d][degree].push_back(overflowPaths[d][stage][i]);
          }

        } else {
          uint32_t max_val = summary[d][stage][i][0];
          uint32_t local_degree = summary[d][stage][i][1];
          uint32_t total_degree = summary[d][stage][i][2];
          vector<uint32_t> local = {static_cast<uint32_t>(stage), local_degree,
                                    total_degree, max_val};
          overflowPaths[d][stage][i].push_back(local);
        }
      }
    }
  }

  std::cout << "[EM_WFCM] ...done!" << std::endl;

  if (0) {
    // Print vc with thresholds
    for (size_t d = 0; d < DEPTH; d++) {
      for (size_t st = 0; st < virtualCounters[d].size(); st++) {
        if (virtualCounters[d][st].size() == 0) {
          continue;
        }
        for (size_t i = 0; i < virtualCounters[d][st].size(); i++) {
          printf("Depth %zu, Degree %zu, Sketch Degree %u, Index %zu ]= Val "
                 "%d \tThresholds: ",
                 d, st, sketchDegrees[d][st][i], i, virtualCounters[d][st][i]);
          for (auto &t : thresholds[d][st][i]) {
            std::cout << "<";
            for (auto &x : t) {
              std::cout << x;
              if (&x != &t.back()) {
                std::cout << ", ";
              }
            }
            std::cout << ">";
            if (&t != &thresholds[d][st][i].back()) {
              std::cout << ", ";
            }
          }
          std::cout << std::endl;
        }
      }
      for (size_t i = 0; i < initFSD[d].size(); i++) {
        if (initFSD[d][i] != 0) {
          printf("Depth %zu, Index %zu ]= Val %d\n", d, i, initFSD[d][i]);
        }
      }
    }
  }

  std::cout << "Maximum degree is: " << maxDegrees[0] << ", " << maxDegrees[1]
            << std::endl;
  std::cout << "Maximum counter value is: " << maxCounterVal << std::endl;

  std::cout << "[EMS_FSD] Initializing EMS_FSD..." << std::endl;
  EM_WFCM EM(thresholds, maxCounterVal, maxDegrees, virtualCounters,
             sketchDegrees, initFSD);
  std::cout << "[EMS_FSD] ...done!" << std::endl;
  auto total_start = std::chrono::high_resolution_clock::now();

  calculateWMRE(EM.ns);
  calculateEntropy(EM.ns);
  writeResEst(0, 0, 0, EM.n_old);
  writeResNs(EM.ns);

  for (size_t iter = 1; iter <= mItersEM; iter++) {
    auto start = std::chrono::high_resolution_clock::now();
    EM.next_epoch();

    auto stop = std::chrono::high_resolution_clock::now();
    auto time = std::chrono::duration_cast<chrono::milliseconds>(stop - start);
    auto total_time =
        std::chrono::duration_cast<chrono::milliseconds>(stop - total_start);

    calculateWMRE(EM.ns);
    calculateEntropy(EM.ns);
    writeResEst(iter, time.count(), total_time.count(), EM.n_old);
    writeResNs(EM.ns);
  }

  mEstFSD = EM.ns;
}

// Gets maps of flows to the first counter layer of FCM
vector<vector<uint32_t>> Waterfall::getInitialDegrees() {
  std::cout << "[WaterfallFCM] Calculate initial degrees from Waterfall..."
            << std::endl;
  vector<vector<uint32_t>> initialDegrees(DEPTH, vector<uint32_t>(W1));
  for (size_t d = 0; d < DEPTH; d++) {
    for (auto &tuple : mUniqueTuples) {
      uint32_t hash_idx = hashing(tuple.num_array, tuple.sz, d) % W1;
      initialDegrees[d][hash_idx]++;
    }
  }

  std::cout << "[WaterfallFCM] ...done!" << std::endl;
  for (size_t d = 0; d < DEPTH; d++) {
    std::cout << "Depth " << d << std::endl;
    for (size_t i = 0; i < initialDegrees.size(); i++) {
      if (initialDegrees[d][i] == 0) {
        continue;
      }
      std::cout << i << ":" << initialDegrees[d][i] << " ";
    }
    std::cout << std::endl;
  }
  return initialDegrees;
}

void Waterfall::calculateWMRE(vector<double> &ns) {
  static uint32_t iter = 1;
  static double d_wmre = 0.0;
  double wmre_nom = 0.0;
  double wmre_denom = 0.0;
  uint32_t max_len = std::max(mTrueFSD.size(), ns.size());
  mTrueFSD.resize(max_len);
  ns.resize(max_len);

  for (size_t i = 0; i < max_len; i++) {
    wmre_nom += std::abs(double(mTrueFSD[i]) - ns[i]);
    wmre_denom += double((double(mTrueFSD[i]) + ns[i]) / 2);
  }
  mWMRE = wmre_nom / wmre_denom;
  std::cout << "[EM WFCM - iter " << iter << "] intermediary wmre " << mWMRE
            << " delta: " << mWMRE - d_wmre << std::endl;
  d_wmre = mWMRE;
  iter++;
}

void Waterfall::calculateEntropy(vector<double> &ns) {
  double entropyEst = 0;

  double totEst = 0;
  double entrEst = 0;

  for (int i = 1; i < ns.size(); ++i) {
    if (ns[i] == 0)
      continue;
    totEst += i * ns[i];
    entrEst += i * ns[i] * log2(i);
  }
  entropyEst = -entrEst / totEst + log2(totEst);

  double entropyTrue = 0;
  double totTrue = 0;
  double entrTrue = 0;
  for (int i = 0; i < mTrueFSD.size(); ++i) {
    if (mTrueFSD[i] == 0)
      continue;
    totTrue += i * mTrueFSD[i];
    entrTrue += i * mTrueFSD[i] * log2(i);
  }
  entropyTrue = -entrTrue / totTrue + log2(totTrue);

  mEntropy = std::abs(entropyEst - entropyTrue) / entropyTrue;
  printf("Entropy Relative Error (RE) = %f (true : %f, est : %f)\n", mEntropy,
         entropyTrue, entropyEst);
}

void Waterfall::setupLogging(string &datasetName) {
  // Setup logging
  mHeaderEst = "Epoch,Estimation Time,Total Time,Weighted Mean Relative Error,"
               "Cardinality,Entropy";
  mHeaderFileOverall = "Average Relative Error,Average Absolute "
                       "Error,Weighted Mean Relative "
                       "Error,F1 Heavy Hitter,Insertions,F1 Member";

  string name_dir = "results/waterfall/";
  if (!std::filesystem::is_directory(name_dir)) {
    std::filesystem::create_directories(name_dir);
  }

  sprintf(mFilePathOverall, "%s/%s.csv", name_dir.c_str(), datasetName.c_str());
  // Remove previous csv file and setup csv file
  std::remove(mFilePathOverall);
  mFileOverall.open(mFilePathOverall, std::ios::out);
  mFileOverall << mHeaderFileOverall << std::endl;

  sprintf(mFilePathEst, "%s/em_%s.csv", name_dir.c_str(), datasetName.c_str());

  std::remove(mFilePathEst);
  mFileEst.open(mFilePathEst, std::ios::out);
  mFileEst << mHeaderEst << std::endl;

  sprintf(mFilePathNs, "%s/ns_%s.dat", name_dir.c_str(), datasetName.c_str());

  std::remove(mFilePathNs);
  mFileNs.open(mFilePathNs, std::ios::out | std::ios::binary);
}

void Waterfall::writeResOverall() {
  // Save data into csv
  char csv[300];
  sprintf(csv, "%.3f,%.3f,%.3f,%.3f,%.3f", mAverageRelativeError,
          mAverageAbsoluteError, mWMRE, mF1HeavyHitter, mF1);
  mFileOverall << csv << std::endl;
}

void Waterfall::writeResEst(uint32_t iter, size_t time, size_t totalTime,
                            double card) {
  // Save data into csv
  char csv[300];
  sprintf(csv, "%u,%ld,%ld,%.6f,%.1f,%.6f", iter, time, totalTime, mWMRE, card,
          mEntropy);
  mFileEst << csv << std::endl;
}

void Waterfall::writeResNs(vector<double> &ns) {
  uint32_t num_entries = 0;
  for (uint32_t i = 0; i < ns.size(); i++) {
    if (ns[i] != 0) {
      num_entries++;
    }
  }
  // Write NS FSD size and then the FSD as uint64_t
  mFileNs.write((char *)&num_entries, sizeof(num_entries));
  for (uint32_t i = 0; i < ns.size(); i++) {
    if (ns[i] != 0) {
      mFileNs.write((char *)&i, sizeof(i));
      mFileNs.write((char *)&ns[i], sizeof(ns[i]));
    }
  }
}
