#ifndef _EMALGORITHM_WATERFALL_FCM_HPP
#define _EMALGORITHM_WATERFALL_FCM_HPP

#include "common.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <codecvt>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <numeric>
#include <ostream>
#include <sys/types.h>
#include <thread>
#include <tuple>
#include <unistd.h>
#include <unordered_map>
#include <vector>
#include <zlib.h>

using std::array;
using std::unordered_map;
using std::vector;

#define NUM_STAGES 3
#define W1 524288        // 8-bit, level 1
#define W2 65536         // 16-bit, level 2
#define W3 8192          // 32-bit, level 3
#define ADD_LEVEL1 255   // 2^8 -2 + 1 (actual count is 254)
#define ADD_LEVEL2 65789 // (2^8 - 2) + (2^16 - 2) + 1 (actual count is 65788)

class EMFSD {
  vector<vector<uint32_t>> counters;
  uint32_t w;                            // width of counters
  vector<double> dist_old, dist_new;     // for ratio \phi
  vector<vector<uint32_t>> counter_dist; // initial counter values
  vector<vector<vector<vector<uint32_t>>>> thresholds;

  array<uint32_t, NUM_STAGES> stage_sz;
  array<uint32_t, W1> stage1;
  array<uint32_t, W2> stage2;
  array<uint32_t, W3> stage3;

  vector<FIVE_TUPLE> tuples; // Found tuples by Waterfall Filter

public:
  vector<double> ns; // for integer n
  double n_sum;
  double card_init; // initial cardinality by MLE
  uint32_t iter = 0;
  bool inited = false;
  EMFSD(array<uint32_t, NUM_STAGES> szes, array<uint32_t, W1> s1,
        array<uint32_t, W2> s2, array<uint32_t, W3> s3,
        vector<FIVE_TUPLE> tuples)
      : stage_sz(szes), stage1(s1), stage2(s2), stage3(s3), tuples(tuples) {

    std::cout << "Init EM_FSD" << std::endl;
    // Calculate Virtual Counters and thresholds

    std::cout << "Hashing tests" << std::endl;
    uint32_t hash_idx1, hash_idx2;
    for (auto &tuple : this->tuples) {
      hash_idx1 = this->hashing(tuple, 0);
      hash_idx2 = this->hashing(tuple, 1);
      std::cout << "Got " << hash_idx1 << " and " << hash_idx2 << std::endl;
    }

    return;
    // Setup distribution and the thresholds for it
    this->counter_dist = vector<vector<uint32_t>>(
        max_degree + 1, vector<uint32_t>(this->max_counter_value + 1, 0));
    this->thresholds.resize(this->counters.size());
    for (size_t d = 0; d < this->counters.size(); d++) {
      if (counters[d].size() == 0) {
        continue;
      }
      this->thresholds[d].resize(this->max_counter_value + 1);
    }
    std::cout
        << "[EM_WATERFALL_FCM] Finished setting up counter_dist and thresholds"
        << std::endl;
    // Inital guess for # of flows
    this->n_new = 0.0; // # of flows (Cardinality)
    for (size_t d = 0; d < this->counters.size(); d++) {
      this->n_new += this->counters[d].size();
      for (size_t i = 0; i < this->counters[d].size(); i++) {
        this->counter_dist[d][counters[d][i]]++;
        /*this->thresholds[d][this->counters[d][i]] = thresh[d][i];*/
      }
    }
    this->w = this->stage_sz[0];

    std::cout << "[EM_WATERFALL_FCM] Initial cardinality guess" << std::endl;
    // Inital guess for Flow Size Distribution (Phi)
    this->dist_new.resize(this->max_counter_value + 1);
    for (auto &degree : this->counters) {
      for (auto count : degree) {
        this->dist_new[count]++;
      }
    }
    std::cout << "[EM_WATERFALL_FCM] Initial Flow Size Distribution guess"
              << std::endl;
    this->ns.resize(this->max_counter_value + 1);
    for (size_t d = 0; d < this->counter_dist.size(); d++) {
      for (size_t i = 0; i < this->counter_dist[d].size(); i++) {
        if (this->counter_dist[d].size() == 0) {
          continue;
        }
        this->dist_new[i] += this->counter_dist[d][i];
        this->ns[i] += this->counter_dist[d][i];
      }
    }
    std::cout << "[EM_WATERFALL_FCM] Normalize guesses" << std::endl;
    // Normalize over inital cardinality
    for (size_t i = 0; i < this->dist_new.size(); i++) {
      this->dist_new[i] /= this->n_new;
    }
    printf("[EM_WATERFALL_FCM] Initial Cardinality : %9.1f\n", this->n_new);
    printf("[EM_WATERFALL_FCM] Max Counter value : %d\n",
           this->max_counter_value);
    printf("[EM_WATERFALL_FCM] Max max degree : %d\n", this->max_degree);
  };

private:
  double n_old,
      n_new; // cardinality
  uint32_t max_counter_value, max_degree;
  struct BetaGenerator {
    int sum;
    int now_flow_num;
    int flow_num_limit;
    vector<int> now_result;
    vector<vector<uint32_t>> thresh;

    explicit BetaGenerator(uint32_t _sum, uint32_t _in_degree,
                           vector<vector<uint32_t>> _thresh)
        : sum(_sum), flow_num_limit(_in_degree), thresh(_thresh) {
      now_flow_num = flow_num_limit;
      now_result.resize(_in_degree);
      now_result[0] = 1;

      if (sum > 600) {
        flow_num_limit = 2;
      } else if (sum > 250)
        flow_num_limit = 3;
      else if (sum > 100)
        flow_num_limit = 4;
      else if (sum > 50)
        flow_num_limit = 5;
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
            // std::ostringstream oss;
            // oss << "Current combi : ";
            // for (auto &x : now_result) {
            //   oss << x << " ";
            // }
            // std::cout << oss.str().c_str() << std::endl;
            return true;
          }
        } else {
          // Extend current combination, e.g. 1 0 to 1 1 0
          now_flow_num++;
          for (int i = 0; i < now_flow_num - 2; ++i) {
            now_result[i] = 1;
          }
          now_result[now_flow_num - 2] = 0;
        }
      }

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
        } else {
          // Shift group to include first entry
          last_group_val = std::accumulate(now_result.end() - last_group_sz + 1,
                                           now_result.end(), 0) +
                           now_result[0];
          if (last_group_val < min_val) {
            return false;
          }
          passes++;
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
          // std::cout << "Invalid permutation: ";
          // for (auto &x : now_result) {
          //   std::cout << x << " ";
          // }
          // std::cout << std::endl;
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

  void calculate_degree(vector<double> &nt, int d) {
    nt.resize(this->max_counter_value + 1);
    std::fill(nt.begin(), nt.end(), 0.0);

    printf("[EM_WATERFALL_FCM] ******** Running for degree %2d with a size of "
           "%12zu\t\t"
           "**********\n",
           d, counter_dist[d].size());

    double lambda = n_old * d / double(w);
    for (uint32_t i = 0; i < counter_dist[d].size(); i++) {
      // enum how to form val:i
      if (counter_dist[d][i] == 0) {
        continue;
      }
      // std::cout << i << std::endl;
      BetaGenerator alpha(i, d, this->thresholds[d][i]),
          beta(i, d, this->thresholds[d][i]);
      double sum_p = 0;
      uint32_t it = 0;
      while (alpha.get_next()) {
        double p = get_p_from_beta(alpha, lambda, dist_old, n_old, d);
        sum_p += p;
        it++;
      }

      if (sum_p == 0.0) {
        if (it > 0) {
          uint32_t temp_val = this->counters[d][i];
          vector<vector<uint32_t>> temp_thresh = this->thresholds[d][i];
          // Start from lowest layer to highest layer
          std::reverse(temp_thresh.begin(), temp_thresh.end());
          for (auto &t : temp_thresh) {
            if (temp_val < t[1] * (t[0] - 1)) {
              break;
            }
            temp_val -= t[1] * (t[0] - 1);
            nt[temp_val] += 1;
          }
        }
      } else {
        while (beta.get_next()) {
          double p = get_p_from_beta(beta, lambda, dist_old, n_old, d);
          for (size_t j = 0; j < beta.now_flow_num; ++j) {
            nt[beta.now_result[j]] += counter_dist[d][i] * p / sum_p;
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
    if (counter_dist[d].size() != 0)
      printf("[EM_WATERFALL_FCM] ******** degree %2d is "
             "finished...(accum:%10.1f #val:%8d)\t**********\n",
             d, accum, (int)this->counters[d].size());
  }

public:
  void next_epoch() {
    auto start = std::chrono::high_resolution_clock::now();
    double lambda = n_old / double(w);
    dist_old = dist_new;
    n_old = n_new;

    vector<vector<double>> nt;
    nt.resize(max_degree + 1);
    std::fill(ns.begin(), ns.end(), 0);

    // Always copy first degree as this is the already perfect estimation
    nt[1].resize(counter_dist[1].size());
    for (size_t i = 0; i < counter_dist[1].size(); i++) {
      nt[1][i] = counter_dist[1][i];
    }
    // Simple Multi thread
    std::thread threads[max_degree + 1];
    for (size_t t = 2; t <= max_degree; t++) {
      threads[t] =
          std::thread(&EMFSD::calculate_degree, *this, std::ref(nt[t]), t);
    }
    for (size_t t = 2; t <= max_degree; t++) {
      threads[t].join();
    }

    // Single threaded
    // for (size_t d = 0; d < nt.size(); d++) {
    //   if (d == 0) {
    //     this->calculate_single_degree(nt[d], d);
    //   } else {
    //     this->calculate_higher_degree(nt[d], d);
    //   }
    // }

    n_new = 0.0;
    for (size_t d = 0; d < nt.size(); d++) {
      // if (nt[d].size() > 0) {
      //   std::cout << "Size of nt[" << d << "] " << nt[d].size() << std::endl;
      // }
      for (uint32_t i = 0; i < nt[d].size(); i++) {
        ns[i] += nt[d][i];
        n_new += nt[d][i];
      }
    }
    for (uint32_t i = 0; i < ns.size(); i++) {
      dist_new[i] = ns[i] / n_new;
    }
    auto stop = std::chrono::high_resolution_clock::now();
    auto time = duration_cast<std::chrono::milliseconds>(stop - start);

    printf("[EM_WATERFALL_FCM - iter %2d] Compute time : %li\n", iter,
           time.count());
    printf("[EM_WATERFALL_FCM - iter %2d] Intermediate cardianlity : %9.1f\n\n",
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
      for (int i = 0; i < 13; ++i) {
        crc = crc32(crc, tuple.num_array + i, 1);
      }
    } else {
      uint32_t msb;
      crc = 0xFFFFFFFF;
      for (size_t i = 0; i < 13; i++) {
        // xor next byte to upper bits of crc
        crc ^= (((uint32_t)tuple.num_array[i]) << 24);
        for (size_t j = 0; j < 8; j++) { // Do eight times.
          msb = crc >> 31;
          crc <<= 1;
          crc ^= (0 - msb) & 0x04C11DB7;
        }
      }
    }
    return crc;
  }
  /*vector<double> get_distribution(std::set<FIVE_TUPLE> tuples,*/
  /*                                std::array<uint32_t, 3> stages_szes,*/
  /*                                uint32_t n_stages) {*/
  /*  // Setup initial degrees for each input counter (stage 0)*/
  /*  vector<uint32_t> init_degree(stages_szes[0]);*/
  /*  uint32_t cht_max_degree = 0;*/
  /*  for (auto &tuple : tuples) {*/
  /*    uint32_t hash_idx =.hashing(tuple, 0);*/
  /*    init_degree[hash_idx]++;*/
  /*    cht_max_degree++;*/
  /*  }*/
  /*  // for (size_t i = 0; i < init_degree.size(); i++) {*/
  /*  //   std::cout << i << ":" << init_degree[i] << " ";*/
  /*  // }*/
  /*  // std::cout << std::endl;*/
  /**/
  /*  uint32_t max_counter_value = 0;*/
  /*  // Summarize sketch and find collisions*/
  /*  // stage, idx, (count, degree, min_value)*/
  /*  vector<vector<vector<uint32_t>>> summary(n_stages);*/
  /*  // stage, idx, layer, (stage, total degree/collisions, local collisions,
   * min*/
  /*  // value)*/
  /*  vector<vector<vector<vector<uint32_t>>>> overflow_paths(n_stages);*/
  /**/
  /*  // Setup sizes for summary and overflow_paths*/
  /*  for (size_t stage = 0; stage < n_stages; stage++) {*/
  /*    summary[stage].resize(.stages_sz[stage], vector<uint32_t>(2, 0));*/
  /*    summary[stage].resize(.stages_sz[stage], vector<uint32_t>(2, 0)); }*/
  /**/
  /*  // Create virtual counters based on degree and count*/
  /*  // degree, count value, n*/
  /*  vector<vector<uint32_t>> virtual_counters(cht_max_degree + 1);*/
  /*  vector<vector<vector<vector<uint32_t>>>> thresholds(cht_max_degree + 1);*/
  /**/
  /*  uint32_t max_degree = 0;*/
  /*  for (size_t stage = 0; stage < n_stages; stage++) {*/
  /*    for (size_t i = 0; i < stages_szes[stage]; i++) {*/
  /*      summary[stage][i][0] = stages[stage][i].count;*/
  /*      // If overflown increase the minimal value for the collisions*/
  /*      if (stages[stage][i].overflow) {*/
  /*        summary[stage][i][0] =.stages[stage][i].max_count;*/
  /*      }*/
  /**/
  /*      if (stage == 0) {*/
  /*        summary[stage][i][1] = init_degree[i];*/
  /*        overflow_paths[stage][i].push_back(*/
  /*            {(uint32_t)stage, init_degree[i], 1, summary[stage][i][0]});*/
  /*      }*/
  /*      // Start checking childeren from stage 1 and up*/
  /*      else {*/
  /*        uint32_t overflown = 0;*/
  /*        // Loop over all childeren*/
  /*        for (size_t k = 0; k < k; k++) {*/
  /*          uint32_t child_idx = i * k + k;*/
  /*          // Add childs count, degree and min_value to current counter*/
  /*          if (.stages[stage - 1][child_idx].overflow) {*/
  /*            summary[stage][i][0] += summary[stage - 1][child_idx][0];*/
  /*            summary[stage][i][1] += summary[stage - 1][child_idx][1];*/
  /*            // If any of my predecessors have overflown, add them to my*/
  /*            // overflown paths*/
  /*            overflown++;*/
  /*            for (size_t j = 0;*/
  /*                 j < overflow_paths[stage - 1][child_idx].size(); j++) {*/
  /*              overflow_paths[stage][i].push_back(*/
  /*                  overflow_paths[stage - 1][child_idx][j]);*/
  /*            }*/
  /*          }*/
  /*        }*/
  /*        // If any of my childeren have overflown, add me to the overflow
   * path*/
  /*        if (overflown > 0) {*/
  /*          vector<uint32_t> imm_overflow = {(uint32_t)stage,*/
  /*                                           summary[stage][i][1],
   * overflown,*/
  /*                                           stages[stage -
   * 1][i].max_count};*/
  /*          overflow_paths[stage][i].insert(overflow_paths[stage][i].begin(),*/
  /*                                          imm_overflow);*/
  /*        }*/
  /*      }*/
  /**/
  /*      // If not overflown and non-zero, we are at the end of the path*/
  /*      if (!.stages[stage][i].overflow && summary[stage][i][0] > 0) {*/
  /*        uint32_t count = summary[stage][i][0];*/
  /*        uint32_t degree = summary[stage][i][1];*/
  /*        // Add entry to VC with its degree [1] and count [0]*/
  /*        virtual_counters[degree].push_back(count);*/
  /*        max_counter_value = std::max(max_counter_value, count);*/
  /*        max_degree = std::max(max_degree, degree);*/
  /**/
  /*        // Remove 1 collsions*/
  /*        // for (size_t j = overflow_paths[stage][i].size() - 1; j > 0; --j)
   * {*/
  /*        //   if (overflow_paths[stage][i][j][2] <= 1) {*/
  /*        // overflow_paths[stage][i].erase(overflow_paths[stage][i].begin()*/
  /*        //     +*/
  /*        //                                    j);*/
  /*        //   }*/
  /*        // }*/
  /**/
  /*        thresholds[degree].push_back(overflow_paths[stage][i]);*/
  /*      }*/
  /*    }*/
  /*  }*/
  /*  for (size_t d = 0; d < thresholds.size(); d++) {*/
  /*    if (thresholds[d].size() == 0) {*/
  /*      continue;*/
  /*    }*/
  /*    // std::cout << "Degree: " << d << '\t';*/
  /*    // for (size_t i = 0; i < thresholds[d].size(); i++) {*/
  /*    //   std::cout << "i " << i << ":";*/
  /*    //   for (size_t l = 0; l < thresholds[d][i].size(); l++) {*/
  /*    //     std::cout << " <";*/
  /*    //     for (auto &col : thresholds[d][i][l]) {*/
  /*    //       std::cout << col;*/
  /*    //       if (&col != &thresholds[d][i][l].back()) {*/
  /*    //         std::cout << ", ";*/
  /*    //       }*/
  /*    //     }*/
  /*    //     std::cout << "> ";*/
  /*    //   }*/
  /*    //   std::cout << std::endl;*/
  /*    // }*/
  /*  }*/
  /**/
  /*  // std::cout << std::endl;*/
  /*  // for (size_t st = 0; st < virtual_counters.size(); st++) {*/
  /*  //   if (virtual_counters[st].size() == 0) {*/
  /*  //     continue;*/
  /*  //   }*/
  /*  //   std::cout << "Degree " << st << " : ";*/
  /*  //   for (auto &val : virtual_counters[st]) {*/
  /*  //     std::cout << " " << val;*/
  /*  //   }*/
  /*  //   std::cout << std::endl;*/
  /*  // }*/
  /*  std::cout << "CHT maximum degree is: " << cht_max_degree << std::endl;*/
  /*  std::cout << "Maximum degree is: " << max_degree << std::endl;*/
  /*  std::cout << "Maximum counter value is: " << max_counter_value <<
   * std::endl;*/
  /**/
  /*  EMFSD EM(.stages_sz, thresholds, max_counter_value, max_degree,
   * max_degree,*/
  /*           virtual_counters);*/
  /*  std::cout << "Initialized EM_FSD, starting estimation..." << std::endl;*/
  /*  for (size_t i = 0; i < em_iters; i++) {*/
  /*    EM.next_epoch();*/
  /*  }*/
  /*  std::cout << "...done!" << std::endl;*/
  /*  vector<double> output = EM.ns;*/
  /*  return output;*/
  /*}*/
};

extern "C" {
EMFSD *EMFSD_new(uint32_t *szes, uint32_t *s1, uint32_t *s2, uint32_t *s3,
                 FIVE_TUPLE *tuples, uint32_t tuples_sz) {
  // Copy all pointer arrays into std::array
  array<uint32_t, NUM_STAGES> stage_szes;
  std::copy_n(szes, NUM_STAGES, stage_szes.begin());

  array<uint32_t, W1> stage1;
  std::copy_n(s1, W1, stage1.begin());

  array<uint32_t, W2> stage2;
  std::copy_n(s2, W2, stage2.begin());

  array<uint32_t, W3> stage3;
  std::copy_n(s3, W3, stage3.begin());

  // Setup tuple list
  std::cout << "Setup FiveTuple vector with size " << tuples_sz << std::endl;
  std::vector<FIVE_TUPLE> tuples_vec(tuples_sz);
  for (size_t i = 0; i < tuples_sz; i++) {
    std::cout << tuples_vec.at(i) << std::endl;
    tuples_vec.at(i) = tuples[i];
    std::cout << tuples_vec.at(i) << std::endl;
  }

  std::cout << "Checking vector with " << tuples_vec.size() << std::endl;
  for (size_t i = 0; i < tuples_vec.size(); i++) {
    std::cout << tuples_vec.at(i) << std::endl;
  }
  return new EMFSD(stage_szes, stage1, stage2, stage3, tuples_vec);
}
void EMFSD_next_epoch(EMFSD *Em_fsd) { Em_fsd->next_epoch(); }
}

#endif
