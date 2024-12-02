#ifndef _EMALGORITHM_WATERFALL_FCM_HPP
#define _EMALGORITHM_WATERFALL_FCM_HPP

#include "common.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <iterator>
#include <numeric>
#include <ostream>
#include <sstream>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>
#include <zlib.h>

using std::array;
using std::unordered_map;
using std::vector;

#define NUM_STAGES 3
#define DEPTH 2
#define K 8
#define W1 524288        // 8-bit, level 1
#define W2 65536         // 16-bit, level 2
#define W3 8192          // 32-bit, level 3
#define ADD_LEVEL1 255   // 2^8 -2 + 1 (actual count is 254)
#define ADD_LEVEL2 65789 // (2^8 - 2) + (2^16 - 2) + 1 (actual count is 65788)
#define OVERFLOW_LEVEL1 255   // 2^8 - 1 maximum count is 254
#define OVERFLOW_LEVEL2 65535 // 2^16 - 1 maximum count is 65536

class EMFSD {
public:
  uint32_t w; // width of counters
  array<vector<vector<uint32_t>>, DEPTH>
      counters;                      // Counters as depth, degree, value
  vector<double> dist_old, dist_new; // for ratio \phi
  array<vector<vector<uint32_t>>, DEPTH> counter_dist; // initial counter values
  array<vector<vector<vector<array<uint32_t, 4>>>>, DEPTH>
      thresholds; // depth, degree, count, < stage, total coll, local
                  // colll, min_value >

  array<uint32_t, NUM_STAGES> stage_szes;
  vector<vector<vector<uint32_t>>> stages; // depth, stage, counter
  vector<FIVE_TUPLE> tuples;               // Found tuples by Waterfall Filter

  array<array<uint32_t, W1>, DEPTH> init_degree;
  array<uint32_t, DEPTH> init_max_degree = {
      0, 0}; // Maximum degree from Waterfall Tables
  array<uint32_t, DEPTH> max_degree = {
      0, 0}; // Maximum degree from FCM Sketch with inital degree from Waterfall

  vector<double> ns; // for integer n
  double n_sum;
  double card_init; // initial cardinality by MLE
  uint32_t iter = 0;
  bool inited = false;
  EMFSD(array<uint32_t, NUM_STAGES> szes,
        vector<vector<vector<uint32_t>>> stages, vector<FIVE_TUPLE> tuples) {

    /*this->tuples.resize(tuples.size());*/
    this->tuples = tuples;
    this->stages = stages;
    this->stage_szes = szes;
    std::cout << "[WaterfallFcm] Stage szes:" << std::endl;
    for (auto &sz : this->stage_szes) {
      std::cout << sz << " ";
    }
    std::cout << std::endl;
    std::cout << "Init EM_FSD" << std::endl;
    // Get inital degree guesses
    for (auto &tuple : this->tuples) {
      for (size_t d = 0; d < DEPTH; d++) {
        uint32_t hash_idx = this->hashing(tuple, d);
        init_degree[d][hash_idx]++;
        init_max_degree[d] =
            std::max(init_max_degree[d], init_degree[d][hash_idx]);
      }
    }
    std::cout << "[WaterfallFcm] Colleted all initial degrees from Waterfall"
              << std::endl;
    for (size_t d = 0; d < DEPTH; d++) {
      std::cout << "[WaterfallFcm] Depth " << d << ":" << std::endl;
      for (size_t i = 0; i < init_degree[d].size(); i++) {
        if (init_degree[d][i] > 0) {
          std::cout << i << ":" << init_degree[d][i] << " ";
          std::cout << ":" << this->stages[d][0][i];
          std::cout << ":" << this->stages[d][1][i / 8];
          std::cout << ":" << this->stages[d][2][i / 8 / 8] << " ";
        }
      }
      std::cout << std::endl;
    }
    // Calculate Virtual Counters and thresholds
    // depth, stage, idx, (count, degree, overflown)
    array<array<vector<array<uint32_t, 3>>, NUM_STAGES>, DEPTH> summary;
    // depth, stage, idx, layer, (stage, total degree/collisions, local
    // collisions, min_value)
    array<array<vector<vector<array<uint32_t, 4>>>, NUM_STAGES>, DEPTH>
        overflow_paths;

    std::cout << "[WaterfallFcm] Setup Summary and Overflow Paths" << std::endl;
    // Setup sizes for summary and overflow_paths
    for (size_t d = 0; d < DEPTH; d++) {
      for (size_t stage = 0; stage < NUM_STAGES; stage++) {
        summary[d][stage].resize(this->stage_szes[stage]);
        overflow_paths[d][stage].resize(this->stage_szes[stage]);
      }
    }

    array<vector<vector<vector<array<uint32_t, 4>>>>, DEPTH>
        init_thresholds; // depth, degree, i, < stage, total coll, local
                         // colll, min_value >
    // Resize to fill all possible degrees
    for (size_t d = 0; d < DEPTH; d++) {
      counters[d].resize(init_max_degree[d] + 1);
      init_thresholds[d].resize(init_max_degree[d] + 1);
    }
    std::cout << "[WaterfallFcm] Created virtual counters and thresholds"
              << std::endl;

    for (size_t d = 0; d < DEPTH; d++) {
      std::cout << "[WaterfallFcm] Look at depth " << d << std::endl;
      for (size_t s = 0; s < NUM_STAGES; s++) {
        for (size_t i = 0; i < this->stage_szes[s]; i++) {
          summary[d][s][i][0] = this->stages[d][s][i];
          // If overflown increase the minimal value for the collisions
          if (s == 0 && summary[d][s][i][0] >= OVERFLOW_LEVEL1) {
            summary[d][s][i][0] = OVERFLOW_LEVEL1 - 1;
            summary[d][s][i][2] = 1;
            std::cout << "Overflown in " << s << ":" << i << std::endl;
          } else if (s == 1 && this->stages[d][s][i] >= OVERFLOW_LEVEL2) {
            summary[d][s][i][0] = OVERFLOW_LEVEL2 - 1;
            summary[d][s][i][2] = 1;
            std::cout << "Overflown in " << s << ":" << i << std::endl;
          }

          if (s == 0) {
            summary[d][s][i][1] = init_degree[d][i];
            overflow_paths[d][s][i].push_back(
                {(uint32_t)s, init_degree[d][i], 1, summary[d][s][i][0]});

          } else {
            if (summary[d][s][i][0] == 0) {
              continue;
            }
            std::cout << "Second stage" << s << " : " << i << std::endl;
            uint32_t overflown = 0;
            // Loop over all childeren
            for (size_t k = 0; k < K; k++) {
              uint32_t child_idx = i * K + k;
              // Add childs count and degree to current counter if they have
              // overflown
              if (summary[d][s - 1][child_idx][2] > 0) {
                std::cout << "Child " << k << std::endl;
                summary[d][s][i][0] += summary[d][s - 1][child_idx][0];
                summary[d][s][i][1] += summary[d][s - 1][child_idx][1];
                // If any of my predecessors have overflown, add them to my
                // overflown paths
                overflown++;
                for (size_t j = 0;
                     j < overflow_paths[d][s - 1][child_idx].size(); j++) {
                  overflow_paths[d][s][i].push_back(
                      overflow_paths[d][s - 1][child_idx][j]);
                }
              }
            }
            // If any of my childeren have overflown, add me to the overflow
            // path
            if (overflown > 0) {
              uint32_t max_count = 0;
              if (s == 1) {
                max_count = ADD_LEVEL1;
              } else {
                max_count = ADD_LEVEL2;
              }
              array<uint32_t, 4> imm_overflow = {
                  (uint32_t)s, summary[d][s][i][1], overflown, max_count};

              overflow_paths[d][s][i].insert(overflow_paths[d][s][i].begin(),
                                             imm_overflow);
              std::cout << "Overflown childeren " << overflown << " at s" << s
                        << " " << i << std::endl;
            }
          }

          // If not overflown and non-zero, we are at the end of the path
          // End of finding path of VC, add it to virtual counter and thresholds
          if (summary[d][s][i][2] == 0 && summary[d][s][i][0] > 0) {
            std::cout << "End of counter, add to VC" << std::endl;
            uint32_t count = summary[d][s][i][0];
            uint32_t degree = summary[d][s][i][1];
            // Add entry to VC with its degree [1] and count [0]
            counters[d][degree].push_back(count);
            max_counter_value = std::max(max_counter_value, count);
            this->max_degree[d] = std::max(this->max_degree[d], degree);

            std::cout << "Remove single collsions" << std::endl;
            // Remove single collsions
            if (overflow_paths[d][s][i].size() != 0) {
              for (int j = overflow_paths[d][s][i].size() - 1; j > 0; --j) {
                if (overflow_paths[d][s][i][j][2] <= 1) {
                  overflow_paths[d][s][i].erase(
                      overflow_paths[d][s][i].begin() + j);
                }
              }
            }

            std::cout << "Add overflow to thresholds" << std::endl;
            init_thresholds[d][degree].push_back(overflow_paths[d][s][i]);
          }
        }
      }
    }

    // Show collision paths for all degrees and counts
    std::cout << "[WaterfallFcm] Setup summary and thresholds" << std::endl;
    std::cout << "[WaterfallFcm] Thresholds:" << std::endl;
    for (auto &threshold : init_thresholds) {
      for (size_t d = 0; d < threshold.size(); d++) {
        if (init_thresholds[d].size() == 0) {
          continue;
        }
        std::cout << "Degree: " << d << std::endl;
        for (size_t i = 0; i < threshold[d].size(); i++) {
          if (threshold[d][i].size() == 0) {
            continue;
          }
          std::cout << "i" << i << ":";
          for (size_t l = 0; l < threshold[d][i].size(); l++) {
            std::cout << " <";
            for (auto &col : threshold[d][i][l]) {
              std::cout << col;
              if (&col != &threshold[d][i][l].back()) {
                std::cout << ", ";
              }
            }
            std::cout << "> ";
          }
          std::cout << std::endl;
        }
      }
      std::cout << std::endl;
    }
    std::cout << std::endl;
    std::cout << "[WaterfallFcm] Counters:" << std::endl;
    for (auto &vc : counters) {
      for (size_t st = 0; st < vc.size(); st++) {
        if (vc[st].size() == 0) {
          continue;
        }
        std::cout << "Degree " << st << " : ";
        for (auto &val : vc[st]) {
          std::cout << " " << val;
        }
        std::cout << std::endl;
      }
    }

    std::cout << "CHT maximum degree is: " << init_max_degree[0] << " and "
              << init_max_degree[1] << std::endl;
    std::cout << "Maximum degree is: " << this->max_degree[0] << " and "
              << this->max_degree[1] << std::endl;
    std::cout << "Maximum counter value is: " << max_counter_value << std::endl;

    // Setup counters and counters_distribution for estimation
    for (size_t d = 0; d < DEPTH; d++) {
      this->counter_dist[d] = vector<vector<uint32_t>>(
          this->max_degree[d] + 1,
          vector<uint32_t>(this->max_counter_value + 1, 0));
      this->thresholds[d].resize(this->counters[d].size());

      for (size_t xi = 0; xi < this->counters[d].size(); xi++) {
        if (this->counters[d][xi].size() == 0) {
          continue;
        }
        this->thresholds[d][xi].resize(this->max_counter_value + 1);
      }
    }
    std::cout << "[EM_WATERFALL_FCM] Finished setting up counter_dist and "
                 "thresholds"
              << std::endl;
    // Inital guess for # of flows, sum total number of counters
    this->n_new = 0.0; // # of flows (Cardinality)
    for (size_t d = 0; d < DEPTH; d++) {
      for (size_t xi = 0; xi < this->counters[d].size(); xi++) {

        this->n_new += this->counters[d][xi].size();

        for (size_t i = 0; i < this->counters[d][xi].size(); i++) {
          this->counter_dist[d][xi][this->counters[d][xi][i]]++;
          this->thresholds[d][xi][this->counters[d][xi][i]] =
              init_thresholds[d][xi][i];
        }
      }
    }
    this->w = this->stage_szes[0];
    // Divide by number of sketches
    this->n_new = this->n_new / double(DEPTH);

    std::cout << "[EM_WATERFALL_FCM] Initial cardinality guess : "
              << this->n_new << std::endl;

    // Inital guess for Flow Size Distribution (Phi)
    this->dist_new.resize(this->max_counter_value + 1);
    for (auto &counters : this->counters) {
      for (auto &degree : counters) {
        for (auto count : degree) {
          this->dist_new[count]++;
        }
      }
    }
    std::cout << "[EM_WATERFALL_FCM] Initial Flow Size Distribution guess"
              << std::endl;
    for (auto &x : this->dist_new) {
      if (x != 0) {
        std::cout << x << " ";
      }
    }
    std::cout << std::endl;

    this->ns.resize(this->max_counter_value + 1);
    for (size_t d = 0; d < DEPTH; d++) {
      for (size_t xi = 0; xi < this->counter_dist[d].size(); xi++) {
        for (size_t i = 0; i < this->counter_dist[d][xi].size(); i++) {
          if (this->counter_dist[d][xi].size() == 0) {
            continue;
          }
          this->dist_new[i] += this->counter_dist[d][xi][i];
          this->ns[i] += this->counter_dist[d][xi][i];
        }
      }
    }
    std::cout << "[EM_WATERFALL_FCM] Summed Flow Size Distribution"
              << std::endl;
    for (auto &x : this->dist_new) {
      if (x != 0) {
        std::cout << x << " ";
      }
    }
    std::cout << std::endl;

    std::cout << "[EM_WATERFALL_FCM] Normalize guesses" << std::endl;
    // Normalize over inital cardinality
    for (size_t i = 0; i < this->dist_new.size(); i++) {
      this->dist_new[i] /= (static_cast<double>(DEPTH) * this->n_new);
      this->ns[i] /= double(DEPTH);
    }
    for (auto &x : this->dist_new) {

      if (x != 0) {
        std::cout << x << " ";
      }
    }
    std::cout << std::endl;

    printf("[EM_WATERFALL_FCM] Initial Cardinality : %9.1f\n", this->n_new);
    printf("[EM_WATERFALL_FCM] Max Counter value : %d\n",
           this->max_counter_value);
    printf("[EM_WATERFALL_FCM] Max degree : %d %d\n", this->max_degree[0],
           this->max_degree[1]);
  };

private:
  double n_old,
      n_new; // cardinality
  uint32_t max_counter_value;
  struct BetaGenerator {
    int sum;
    int now_flow_num;
    int flow_num_limit;
    vector<int> now_result;
    vector<array<uint32_t, 4>> thresh;

    explicit BetaGenerator(uint32_t _sum, uint32_t _in_degree,
                           vector<array<uint32_t, 4>> _thresh)
        : sum(_sum), flow_num_limit(_in_degree), thresh(_thresh) {

      now_flow_num = flow_num_limit;
      now_result.resize(_in_degree);
      for (size_t i = 0; i < _in_degree - 1; i++) {
        now_result[i] = 1;
      }

      if (sum > 600) {
        flow_num_limit = 2;
      } else if (sum > 250) {
        flow_num_limit = std::min(3, flow_num_limit);
      } else if (sum > 100) {
        flow_num_limit = std::min(4, flow_num_limit);
      } else if (sum > 50) {
        flow_num_limit = std::min(5, flow_num_limit);
      }
      // else
      //   flow_num_limit = 6;
    }

    bool get_new_comb() {
      for (int j = now_flow_num - 2; j >= 0; --j) {
        int t = ++now_result[j];
        for (int k = j + 1; k < now_flow_num - 1; ++k) {
          now_result[k] = t;
        }
        int partial_sum = 0;
        for (int k = 0; k < now_flow_num - 1; ++k) {
          partial_sum += now_result[k];
        }
        int remain = sum - partial_sum;
        if (remain >= now_result[now_flow_num - 2]) {
          now_result[now_flow_num - 1] = remain;
          return true;
        }
      }

      return false;
    }

    bool get_next() {
      while (now_flow_num <= flow_num_limit) {
        now_result.resize(now_flow_num);
        if (get_new_comb()) {
          if (check_condition()) {
            /*std::ostringstream oss;*/
            /*oss << "Current combi : ";*/
            /*for (auto &x : now_result) {*/
            /*  oss << x << " ";*/
            /*}*/
            /*std::cout << oss.str().c_str() << std::endl;*/
            return true;
          }
        } else {
          // Extend current combination, e.g. 1 0 to 1 1 0
          now_flow_num++;
          if (now_flow_num >= flow_num_limit) {
            break;
          }
          for (int i = 0; i < now_flow_num - 2; ++i) {
            now_result[i] = 1;
          }
          now_result[now_flow_num - 2] = 0;
        }
      }

      std::cout << "Done with get_next()" << std::endl;
      std::ostringstream oss;
      oss << "Ending now_result : ";
      for (auto &x : now_result) {
        oss << x << " ";
      }
      std::cout << oss.str().c_str() << std::endl;
      return false;
    }

    bool check_condition() {
      // return true;
      for (auto &t : thresh) {
        uint32_t colls = t[2];
        if (colls <= 1) {
          continue;
        }

        uint32_t tot_curr_colls = t[2];
        uint32_t group_sz = (uint32_t)now_flow_num / tot_curr_colls;
        uint32_t last_group_sz = std::ceil(now_flow_num / tot_curr_colls);
        uint32_t min_val = t[3];
        uint32_t passes = 0;
        uint32_t last_group_val = std::accumulate(
            now_result.end() - last_group_sz, now_result.end(), 0);

        // Remainder is larger then minimal value thus 1 pass
        if (last_group_val >= min_val) {
          passes++;
          for (size_t i = 0; i < tot_curr_colls - 1; i++) {
            uint32_t accum =
                std::accumulate(now_result.begin() + i * group_sz,
                                now_result.begin() + (i + 1) * group_sz, 0);
            if (accum >= min_val) {
              passes++;
            }
          }
        }
        // Remainder not larger than minimal value
        // Shift group to include first entry
        else {
          last_group_val = std::accumulate(now_result.end() - last_group_sz + 1,
                                           now_result.end(), 0) +
                           now_result[0];
          if (last_group_val < min_val) {
            return false;
          }
          passes++;
          std::cout << "pre else for loop" << std::endl;
          for (size_t i = 0; i < tot_curr_colls - 1; i++) {
            uint32_t accum =
                std::accumulate(now_result.begin() + 1 + i * group_sz,
                                now_result.begin() + 1 + (i + 1) * group_sz, 0);
            if (accum >= min_val) {
              passes++;
            }
          }
        }
        // Combination has not large enough values to meet all conditions
        // E.g. it needs have 2 values large than the L2 threshold +
        // predecessor (min_value)
        if (passes < colls) {
          //  std::cout << "Invalid permutation: ";
          //  for (auto &x : now_result) {
          //    std::cout << x << " ";
          //  }
          //  std::cout << std::endl;
          return false;
        }
      }
      return true;
    }
  };

  int factorial(int n) {
    if (n == 0 || n == 1)
      return 1;
    return factorial(n - 1) * n;
  }

  double get_p_from_beta(BetaGenerator &bt, double lambda,
                         vector<double> now_dist, double now_n,
                         uint32_t degree) {
    std::unordered_map<uint32_t, uint32_t> mp;
    for (int i = 0; i < bt.now_flow_num; ++i) {
      mp[bt.now_result[i]]++;
    }

    double ret = std::exp(-lambda);
    for (auto &kv : mp) {
      uint32_t fi = kv.second;
      uint32_t si = kv.first;
      double lambda_i = now_n * (now_dist[si] * degree) / w;
      ret *= (std::pow(lambda_i, fi)) / factorial(fi);
      // if (lambda_i > 0) {
      //   for (auto &x : bt.now_result) {
      //     std::cout << x << " ";
      //   }
      //   std::cout << std::endl;
      //   std::cout << lambda_i << " " << now_dist[si] << std::endl;
      //   std::cout << si << " " << fi << " >> " << ret << std::endl;
      // }
    }

    return ret;
  }

  void calculate_degree(vector<double> &nt, int d, int xi) {
    nt.resize(this->max_counter_value + 1);
    std::fill(nt.begin(), nt.end(), 0.0);

    printf("[EM_WATERFALL_FCM] ******** Running for degree %2d with a size of "
           "%12zu\t\t"
           "**********\n",
           xi, counter_dist[d][xi].size());

    double lambda = n_old * xi / double(w);
    for (uint32_t i = 0; i < counter_dist[d][xi].size(); i++) {
      if (counter_dist[d][xi][i] == 0) {
        continue;
      }
      std::cout << "Found value " << i << " with count of "
                << counter_dist[d][xi][i] << std::endl;

      BetaGenerator alpha(i, xi, this->thresholds[d][xi][i]),
          beta(i, xi, this->thresholds[d][xi][i]);
      double sum_p = 0.0;
      uint32_t it = 0;
      // Sum over first combinations
      while (alpha.get_next()) {
        double p = get_p_from_beta(alpha, lambda, dist_old, n_old, xi);
        sum_p += p;
        it++;
      }

      // If no results, but I did have combinations, spread out the value
      if (sum_p == 0.0) {
        if (it > 0) {
          uint32_t temp_val = i;
          vector<array<uint32_t, 4>> temp_thresh = this->thresholds[d][xi][i];
          // Start from lowest layer to highest layer
          std::reverse(temp_thresh.begin(), temp_thresh.end());
          for (auto &t : temp_thresh) {
            if (temp_val < t[1] * (t[0] - 1)) {
              break;
            }
            temp_val -= t[1] * (t[0] - 1);
          }
          if (temp_val >= 0 and temp_val <= this->max_counter_value) {
            nt[temp_val] += 1;
          }
        }
      } else {
        while (beta.get_next()) {
          double p = get_p_from_beta(beta, lambda, dist_old, n_old, xi);
          for (size_t j = 0; j < beta.now_flow_num; ++j) {
            nt[beta.now_result[j]] += counter_dist[d][xi][i] * p / sum_p;
          }
        }
      }
    }

    double accum = std::accumulate(nt.begin(), nt.end(), 0.0);
    if (accum != accum) {
      std::cout << "Accum is Nan" << std::endl;
      for (auto &x : nt) {
        std::cout << x << " ";
      }
      std::cout << std::endl;
    }
    if (this->counter_dist[d][xi].size() != 0)
      printf("[EM_WATERFALL_FCM] ******** degree %2d is "
             "finished...(accum:%10.1f #val:%8d)\t**********\n",
             xi, accum, (int)this->counter_dist[d][xi].size());
  }

public:
  void next_epoch() {
    auto start = std::chrono::high_resolution_clock::now();
    std::cout << "[EM_WATERFALL_FCM] Start next epoch" << std::endl;

    n_old = n_new;
    dist_old = dist_new;

    array<vector<vector<double>>, DEPTH> nt;
    std::fill(ns.begin(), ns.end(), 0);

    std::cout << "[EM_WATERFALL_FCM] Copy first degree distribution"
              << std::endl;
    // Always copy first degree as this is can be considered a perfect
    // estimation. qWaterfall is not perfect, but assumed to be
    for (size_t d = 0; d < DEPTH; d++) {
      nt[d].resize(this->max_degree[d] + 1);
      nt[d][1].resize(counter_dist[d][1].size());
      for (size_t i = 0; i < counter_dist[d][1].size(); i++) {
        nt[d][1][i] += counter_dist[d][1][i];
      }
    }

    std::cout << "[EM_WATERFALL_FCM] Init first degree" << std::endl;
    // Simple Multi thread
    uint32_t total_degree = this->max_degree[0] + this->max_degree[1];
    std::thread threads[total_degree];

    std::cout << "[EM_WATERFALL_FCM] Created " << total_degree << " threads"
              << std::endl;
    for (size_t d = 0; d < DEPTH; d++) {
      for (size_t t = 2; t <= this->max_degree[d]; t++) {
        std::cout << "Start thread " << t << std::endl;
        threads[t] = std::thread(&EMFSD::calculate_degree, *this,
                                 std::ref(nt[d][t]), d, t);
      }
      for (size_t t = 2; t <= this->max_degree[d]; t++) {
        threads[t].join();
      }
    }

    // Single threaded
    // for (size_t d = 0; d < DEPTH; d++) {
    //  for (size_t xi = 2; xi <= this->max_degree[d]; xi++) {
    //    this->calculate_degree(nt[d][xi], d, xi);
    //  }
    //}

    std::cout << "[EM_WATERFALL_FCM] Finished calculating nt per degree"
              << std::endl;

    for (size_t d = 0; d < DEPTH; d++) {
      for (size_t xi = 0; xi < nt[d].size(); xi++) {
        // if (nt[d].size() > 0) {
        //   std::cout << "Size of nt[" << d << "] " << nt[d].size() <<
        //   std::endl;
        // }
        for (uint32_t i = 0; i < nt[d][xi].size(); i++) {
          ns[i] += nt[d][xi][i];
        }
      }
    }

    n_new = 0.0;
    for (size_t i = 0; i < ns.size(); i++) {
      if (ns[i] != 0) {
        ns[i] /= static_cast<double>(DEPTH);
        n_new += ns[i];
      }
    }
    for (uint32_t i = 0; i < ns.size(); i++) {
      dist_new[i] = ns[i] / n_new;
    }
    auto stop = std::chrono::high_resolution_clock::now();
    auto time =
        std::chrono::duration_cast<std::chrono::milliseconds>(stop - start);

    printf("[EM_WATERFALL_FCM - iter %2d] Compute time : %li\n", iter,
           time.count());
    printf("[EM_WATERFALL_FCM - iter %2d] Intermediate cardianlity : "
           "%9.1f\n\n",
           iter, n_new);
    iter++;
    // std::cout << "ns : ";
    // for (size_t i = 0; i < ns.size(); i++) {
    //   if (ns[i] != 0) {
    //     std::cout << i << " = " << ns[i] << "\t";
    //   }
    // }
    // std::cout << std::endl;
  }

  uint32_t hashing(FIVE_TUPLE tuple, uint32_t depth) {
    uint32_t crc = 0;
    if (depth == 0) {
      crc = crc32(0L, Z_NULL, 0);
      crc = crc32(crc, tuple.num_array, 13);
    } else {
      crc = 0xF0000000;
      crc = crc32(crc, tuple.num_array, 13);
      std::cout << "crc " << crc % W1 << std::endl;
    }
    return crc % W1;
  }
};

extern "C" {
void *EMFSD_new(uint32_t *szes, uint32_t *s1_1, uint32_t *s1_2, uint32_t *s2_1,
                uint32_t *s2_2, uint32_t *s3_1, uint32_t *s3_2,
                FIVE_TUPLE *tuples, uint32_t tuples_sz) {

  std::cout << "[WaterfallFcm CTypes] Start parsing python to c" << std::endl;
  array<uint32_t, NUM_STAGES> stage_szes;
  std::copy_n(szes, NUM_STAGES, stage_szes.begin());

  std::cout << "\tdone stage sizes" << std::endl;
  vector<vector<vector<uint32_t>>> stages(DEPTH,
                                          vector<vector<uint32_t>>(NUM_STAGES));
  std::cout << "\tcopy stages" << std::endl;
  for (size_t d = 0; d < DEPTH; d++) {
    stages[d][0].resize(W1);
    stages[d][1].resize(W2);
    stages[d][2].resize(W3);
  }
  std::copy_n(s1_1, W1, stages[0][0].begin());
  std::copy_n(s2_1, W2, stages[0][1].begin());
  std::copy_n(s3_1, W3, stages[0][2].begin());
  std::copy_n(s1_2, W1, stages[1][0].begin());
  std::copy_n(s2_2, W2, stages[1][1].begin());
  std::copy_n(s3_2, W3, stages[1][2].begin());
  std::cout << "\tdone stages" << std::endl;

  // Setup tuple list
  std::cout << "[WaterfallFcm CTypes] Setup FiveTuple vector with size "
            << tuples_sz << std::endl;
  std::vector<FIVE_TUPLE> tuples_vec(tuples_sz);
  for (size_t i = 0; i < tuples_sz; i++) {
    tuples_vec.at(i) = tuples[i];
  }

  std::cout << "[WaterfallFcm CTypes] Checking vector with "
            << tuples_vec.size() << std::endl;
  for (size_t i = 0; i < tuples_vec.size(); i++) {
    std::cout << i << " : " << tuples_vec.at(i) << std::endl;
  }
  return new EMFSD(stage_szes, stages, tuples_vec);
}

void EMFSD_next_epoch(void *ptr) {
  EMFSD *em = reinterpret_cast<EMFSD *>(ptr);
  em->next_epoch();
}

vector<double> *get_ns(void *ptr) {
  EMFSD *em = reinterpret_cast<EMFSD *>(ptr);
  return &em->ns;
}

size_t vector_size(vector<double> *v) { return v->size(); }
double vector_get(vector<double> *v, size_t i) { return v->at(i); }
}

#endif
