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
#include <limits>
#include <numeric>
#include <ostream>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>
#include <zlib.h>

using std::array;
using std::unordered_map;
using std::vector;

class EM_WFCM {
private:
  vector<vector<vector<vector<vector<uint32_t>>>>>
      thresholds; // depth, degree, count, < stage, total coll, local
                  // colll, min_value >
  vector<vector<vector<uint32_t>>> counters;
  vector<vector<vector<uint32_t>>> sketch_degrees;
  vector<vector<uint32_t>> init_fsd;
  vector<vector<vector<uint32_t>>> counter_dist; // initial counter values
  vector<double> dist_old, dist_new;             // for ratio \phi
  uint32_t w = W1;                               // width of counters
public:
  vector<double> ns; // for integer n
  uint32_t max_counter_value = 0;
  vector<uint32_t> max_degree;
  bool inited = false;
  double n_old,
      n_new; // cardinality
  double n_sum;
  double card_init; // initial cardinality by MLE
  uint32_t iter = 0;
  EM_WFCM(vector<vector<vector<vector<vector<uint32_t>>>>> &_thresh,
          uint32_t in_max_value, vector<uint32_t> max_degree,
          vector<vector<vector<uint32_t>>> &_counters,
          vector<vector<vector<uint32_t>>> &_sketch_degrees,
          vector<vector<uint32_t>> &_init_fsd)
      : thresholds(_thresh), counters(_counters),
        sketch_degrees(_sketch_degrees), init_fsd(_init_fsd),
        counter_dist(DEPTH), max_counter_value(in_max_value),
        max_degree(max_degree) {

    std::cout << "[EM_WFCM] Init EMFSD" << std::endl;

    // Setup counters and counters_distribution for estimation, counter_dist is
    // Depth, Degree, Count
    for (size_t d = 0; d < DEPTH; d++) {
      this->counter_dist[d].resize(this->max_degree[d] + 1);

      for (size_t xi = 0; xi < this->counter_dist[d].size(); xi++) {
        this->counter_dist[d][xi].resize(this->max_counter_value + 1);
      }
    }
    std::cout << "[EM_WFCM] Finished setting up counter_dist and "
                 "thresholds"
              << std::endl;
    // Inital guess for # of flows, sum total number of counters
    this->n_new = 0.0; // # of flows (Cardinality)
    for (size_t d = 0; d < DEPTH; d++) {
      for (size_t xi = 0; xi < counters[d].size(); xi++) {
        this->n_new += counters[d][xi].size();

        for (size_t i = 0; i < counters[d][xi].size(); i++) {
          this->counter_dist[d][xi][counters[d][xi][i]]++;
        }
      }

      // Add inital fsd
      for (size_t i = 0; i < this->init_fsd[d].size(); i++) {
        this->n_new += this->init_fsd[d][i];
      }
    }

    // Divide by number of sketches
    this->n_new = this->n_new / double(DEPTH);

    std::cout << "[EM_WFCM] Initial cardinality guess : " << this->n_new
              << std::endl;

    // Inital guess for Flow Size Distribution (Phi)
    this->dist_new.resize(this->max_counter_value + 1);
    this->dist_old.resize(this->max_counter_value + 1);

    this->ns.resize(this->max_counter_value + 1);
    for (size_t d = 0; d < DEPTH; d++) {
      for (size_t xi = 0; xi < this->counter_dist[d].size(); xi++) {
        for (size_t i = 0; i < this->counter_dist[d][xi].size(); i++) {
          this->dist_new[i] += this->counter_dist[d][xi][i];
          this->ns[i] += this->counter_dist[d][xi][i];
        }
      }
      // Add inital fsd
      for (size_t i = 0; i < this->init_fsd[d].size(); i++) {
        this->dist_new[i] += init_fsd[d][i];
        this->ns[i] += init_fsd[d][i];
      }
    }

    std::cout << "[EM_WFCM] Initial Flow Size Distribution guess" << std::endl;
    /*for (size_t i = 0; i < this->dist_new.size(); i++) {*/
    /*  if (this->dist_new[i] != 0) {*/
    /*    std::cout << i << ":" << this->dist_new[i] << " ";*/
    /*  }*/
    /*}*/
    /*std::cout << std::endl;*/

    std::cout << "[EM_WFCM] Normalize guesses" << std::endl;
    // Normalize over inital cardinality
    for (size_t i = 0; i < this->dist_new.size(); i++) {
      this->dist_new[i] /= (static_cast<double>(DEPTH) * this->n_new);
      this->ns[i] /= double(DEPTH);
    }

    /*for (size_t i = 0; i < this->dist_new.size(); i++) {*/
    /*  if (this->dist_new[i] != 0) {*/
    /*    std::cout << i << ":" << this->dist_new[i] << " ";*/
    /*  }*/
    /*}*/
    /*std::cout << std::endl;*/

    printf("[EM_WFCM] Initial Cardinality : %9.1f\n", this->n_new);
    printf("[EM_WFCM] Max Counter value : %d\n", this->max_counter_value);
    printf("[EM_WFCM] Max degree : %d, %d\n", this->max_degree[0],
           this->max_degree[1]);
  }

private:
  struct BetaGenerator {
    int sum;
    int max_small = 0;
    int now_flow_num;
    int flow_num_limit;
    int start_pos;
    uint32_t in_degree;
    uint32_t in_sketch_degree;
    uint32_t total_combi = 0;
    vector<int> now_result;
    vector<vector<uint32_t>> thresh;
    uint32_t min_val = -1;
    uint32_t min_count = 0;
    uint32_t max_val = 0;
    uint32_t max_count = 0;

    explicit BetaGenerator(uint32_t _sum, uint32_t _in_degree,
                           uint32_t _in_sketch_degree,
                           vector<vector<uint32_t>> _thresh)
        : sum(_sum), in_degree(_in_degree), in_sketch_degree(_in_sketch_degree),
          thresh(_thresh) {

      flow_num_limit = in_degree;
      max_small = sum;

      // Setup limit for degree 1
      if (in_sketch_degree > 1) {
        flow_num_limit = in_sketch_degree;

        // Note that the counters of degree D (> 1) includes at least D flows.
        // But, the combinatorial complexity also increases to O(N^D), which
        // takes a lot of time. To truncate the compexity, we introduce a
        // simplified version that prohibits the number of flows less than D. As
        // explained in the paper, its effect is quite limited as the number of
        // such counters are only a few.

        // "simplified" keeps the original in_degree
        // 15k, 15k, 7k
        if (in_sketch_degree >= 4 and sum < 10000) {
          flow_num_limit = 3;
        } else if (in_sketch_degree >= 4 and sum >= 10000) {
          flow_num_limit = 2;
        } else if (in_sketch_degree == 3 and sum >= 5000) {
          flow_num_limit = 2;
        }
        // Limit to the in_degree
        for (auto &t : thresh) {
          if (t[3] == max_val) {
            max_count++;
          } else if (t[3] > max_val) {
            max_count = 1;
            max_val = t[3];
          }
          if (t[3] == min_val) {
            min_count++;
          } else if (t[3] < min_val) {
            min_count = 1;
            min_val = t[3];
          }
        }
        /*std::cout << "Min val " << sum_thresh[1][1] << " with count "*/
        /*          << sum_thresh[1][0] << std::endl;*/
        /*std::cout << "Max val " << sum_thresh[0][1] << " with count "*/
        /*          << sum_thresh[0][0] << std::endl;*/
      } else {
        if (sum > 600) { // 1000 for large data, 600 for small data
          flow_num_limit = 2;
        } else if (sum > 250) // 500 for large data, 250 for small data
          flow_num_limit = 3;
        else if (sum > 100)
          flow_num_limit = 3;
        else if (sum > 50)
          flow_num_limit = 3;
        else
          flow_num_limit = 3;
        /*if (sum > 600) { // 1000 for large data, 600 for small data*/
        /*  flow_num_limit = 2;*/
        /*} else if (sum > 100) // 500 for large data, 250 for small data*/
        /*{*/
        /*  flow_num_limit = 3;*/
        /*} else if (sum > 50) {*/
        /*  flow_num_limit = 4;*/
        /*} else {*/
        /*  flow_num_limit = in_degree;*/
        /*}*/
      }
      flow_num_limit = std::min((int)in_degree, flow_num_limit);
      /*printf("Setup gen with sum:%d, in_degree:%d, in_sketch_degree:%d, "*/
      /*       "flow_num_limit:%d\n",*/
      /*       sum, in_degree, in_sketch_degree, flow_num_limit);*/
      now_flow_num = flow_num_limit;
      now_result.resize(now_flow_num);
    }

    bool get_new_comb() {
      for (int j = now_flow_num - 2; j >= 0; --j) {
        int t = now_result[j]++;
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
          /*print_now_result();*/
          return true;
        }
      }

      /*print_now_result();*/
      return false;
    }

    bool get_next() {
      while (now_flow_num <= flow_num_limit) {
        if (get_new_comb()) {
          if (check_condition()) {
            /*print_now_result();*/
            total_combi++;
            return true;
          }
        } else { // no more combination -> go to next flow number
          now_flow_num++;
        }
      }
      return false;
    }

    void print_now_result() {
      std::cout << "Now result[" << now_result.size() << "] ";
      for (auto &x : now_result) {
        std::cout << x << " ";
      }
      std::cout << std::endl;
    }

    void print_thresholds() {
      std::cout << "Threshold[" << thresh.size() << "] ";
      for (auto &t : thresh) {
        std::cout << " <";
        for (auto &x : t) {
          std::cout << x;
          if (&x != &t.back()) {
            std::cout << ", ";
          }
        }
        std::cout << "> ";
      }
      std::cout << std::endl;
    }

    void print_thresh(auto t) {
      std::cout << "Thresh ";
      std::cout << " <";
      for (auto &x : t) {
        std::cout << x;
        if (&x != &t.back()) {
          std::cout << ", ";
        }
      }
      std::cout << "> ";
      std::cout << std::endl;
    }

    bool check_condition() {
      if (in_sketch_degree == 1) {
        return true;
      }

      uint32_t last_val = now_result.back();
      if (last_val >= max_val) {
        uint32_t n_spread = 1;
        if (now_flow_num > in_sketch_degree) {
          n_spread = now_flow_num - in_sketch_degree;
        }
        // First group takes whole spread
        uint32_t first_val = std::accumulate(now_result.begin(),
                                             now_result.begin() + n_spread, 0);
        if (first_val < min_val * min_count) {
          return false;
        }

        // Combination passes minimal requiremnt of 2 flows
        if (now_flow_num <= 2) {
          return true;
        }

        // Check inner numbers in combination
        for (size_t i = n_spread; i < now_flow_num - 1; i++) {
          if (now_result[i] < min_val) {
            return false;
          }
        }
        return true;
      } else {
        last_val += now_result[0];
        if (last_val < max_val) {
          return false;
        }

        uint32_t n_spread = 1;
        if (now_flow_num > in_sketch_degree) {
          n_spread = now_flow_num - in_sketch_degree - 1;
        }
        uint32_t first_val = std::accumulate(
            now_result.begin() + 1, now_result.begin() + 1 + n_spread, 0);
        if (first_val < min_val) {
          return false;
        }

        // Combination passes minimal requiremnt of 2 flows
        if (now_flow_num <= 2) {
          return true;
        }

        // Check inner numbers in combination
        for (size_t i = 1 + n_spread; i < now_flow_num - 1; i++) {
          if (now_result[i] < min_val) {
            return false;
          }
        }
        return true;
      }
      return true;
    }
  };

  static constexpr int factorial(int n) {
    if (n == 0 || n == 1)
      return 1;
    return factorial(n - 1) * n;
  }

  double get_p_from_beta(BetaGenerator &bt, double lambda,
                         vector<double> &now_dist, double now_n,
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
    }

    return ret;
  }

  void calculate_degree(vector<double> &nt, int d, int xi) {

    if (this->counters[d].size() <= xi) {
      return;
    }
    printf("[EM_WFCM] ******** Running for degree %2d with a size of "
           "%12zu\t\t"
           "**********\n",
           xi, this->counters[d][xi].size());

    nt.resize(this->max_counter_value + 1);
    std::fill(nt.begin(), nt.end(), 0.0);

    for (uint32_t i = 0; i < this->counters[d][xi].size(); i++) {
      if (this->counters[d][xi][i] == 0) {
        continue;
      }
      double lambda = this->n_old * xi / static_cast<double>(w);

      uint32_t sum = this->counters[d][xi][i];
      uint32_t sketch_xi = this->sketch_degrees[d][xi][i];
      uint32_t est_xi = sketch_xi; // Est_xi is sketch_xi as for each input
                                   // counter a VC should be calculated
      vector<vector<uint32_t>> &thresh = this->thresholds[d][xi][i];
      /*std::cout << "Found val " << this->counters[d][xi][i] << std::endl;*/
      /*if (this->thresholds[d][xi].size() <= i) {*/
      /*  std::cout << "ERROR out of threshold length" << std::endl;*/
      /*  exit(1);*/
      /*}*/
      /*for (auto &t : thresh) {*/
      /*  std::cout << "<";*/
      /*  for (auto &x : t) {*/
      /*    std::cout << x;*/
      /*    if (&x != &t.back()) {*/
      /*      std::cout << ", ";*/
      /*    }*/
      /*  }*/
      /*  std::cout << "> ";*/
      /*}*/
      /*std::cout << std::endl;*/

      BetaGenerator alpha(sum, xi, sketch_xi, thresh),
          beta(sum, xi, sketch_xi, thresh);
      double sum_p = 0.0;
      uint32_t it = 0;

      while (alpha.get_next()) {
        double p =
            get_p_from_beta(alpha, lambda, this->dist_old, this->n_old, est_xi);
        sum_p += p;
        it++;
      }
      // Sum over first combinations

      /*std::cout << "Val " << sum << " found sum_p " << sum_p << " with "*/
      /*          << alpha.total_combi << " combinations" << std::endl;*/
      /**/
      // If there where valid combinations, but value of combinations where
      // not found in measured data. We
      if (sum_p == 0.0) {
        if (sketch_xi <= 1) {
          continue;
        }
        if (it > 0) {
          uint32_t temp_val = sum;

          /*std::cout << "adjust value at " << i << " with val " << temp_val*/
          /*          << std::endl;*/
          /*alpha.print_thresholds();*/

          // Remove l1 collisions, keep one flow
          temp_val -= thresh.back()[3] * (sketch_xi - 1);
          if (thresh.size() == 3) {
            temp_val -= thresh[1][3] * (thresh[1][1] - 1);
          }

          /*std::cout << "Storing 1 at " << temp_val << std::endl;*/
          nt[temp_val] += 1;
        }
      } else {
        while (beta.get_next()) {
          double p = get_p_from_beta(beta, lambda, this->dist_old, this->n_old,
                                     est_xi);
          for (size_t j = 0; j < beta.now_flow_num; ++j) {
            nt[beta.now_result[j]] += p / sum_p;
          }
        }
      }
    }

    double accum = std::accumulate(nt.begin(), nt.end(), 0.0);
    if (0) {
      for (size_t i = 0; i < nt.size(); i++) {
        if (nt[i] != 0) {
          std::cout << i << ":" << nt[i] << " ";
        }
      }
      std::cout << std::endl;
    }
    if (this->counters[d][xi].size() != 0)
      printf("[EM_WFCM] ******** depth %d degree %2d is "
             "finished...(accum:%10.1f #val:%8d)\t**********\n",
             d, xi, accum, (int)this->counters[d][xi].size());
  }

public:
  void next_epoch() {
    std::cout << "[EM_WFCM] Start next epoch" << std::endl;
    auto start = std::chrono::high_resolution_clock::now();

    this->n_old = this->n_new;
    this->dist_old = this->dist_new;

    std::cout << "[EM_WFCM] Copy first degree distribution" << std::endl;

    // Always copy first degree as this is can be considered a perfect
    // estimation. qWaterfall is not perfect, but assumed to be
    vector<vector<vector<double>>> nt(DEPTH);
    for (size_t d = 0; d < DEPTH; d++) {
      nt[d].resize(this->max_degree[d] + 1);
    }
    std::fill(this->ns.begin(), this->ns.end(), 0);

    if (1) {
      // Simple Multi thread
      vector<vector<std::thread>> threads(DEPTH);
      for (size_t d = 0; d < threads.size(); d++) {
        threads[d].resize(this->max_degree[d] + 1);
      }

      uint32_t total_degree = this->max_degree[0] + this->max_degree[1];
      std::cout << "[EM_WFCM] Created " << total_degree << " threads"
                << std::endl;
      for (size_t d = 0; d < DEPTH; d++) {
        for (size_t t = 0; t < threads[d].size(); t++) {
          /*if (this->counters[d][t].size() == 0) {*/
          /*  continue;*/
          /*}*/
          std::cout << "[EM_WFCM] Start thread " << t << " at depth " << d
                    << std::endl;
          threads[d][t] = std::thread(&EM_WFCM::calculate_degree, *this,
                                      std::ref(nt[d][t]), d, t);
        }
      }

      std::cout << "[EM_WFCM] Started all threads, wait for them to finish..."
                << std::endl;

      for (size_t d = 0; d < DEPTH; d++) {
        for (size_t t = 0; t < threads[d].size(); t++) {
          /*if (this->counters[d][t].size() == 0) {*/
          /*  continue;*/
          /*}*/
          threads[d][t].join();
        }
      }
    } else {
      // Single threaded
      for (size_t d = 0; d < DEPTH; d++) {
        for (size_t xi = 1; xi <= this->max_degree[d]; xi++) {
          if (this->counters[d][xi].size() == 0) {
            continue;
          }
          std::cout << "[EM_WFCM] Start calculating " << xi << " at depth " << d
                    << std::endl;
          this->calculate_degree(nt[d][xi], d, xi);
        }
      }
    }

    std::cout << "[EM_WFCM] Finished calculating nt per degree" << std::endl;

    for (size_t d = 0; d < DEPTH; d++) {
      for (size_t xi = 0; xi < nt[d].size(); xi++) {
        for (uint32_t i = 0; i < nt[d][xi].size(); i++) {
          this->ns[i] += nt[d][xi][i];
        }
      }
      for (size_t i = 0; i < this->init_fsd[d].size(); i++) {
        this->ns[i] += this->init_fsd[d][i];
      }
    }

    this->n_new = 0.0;
    for (size_t i = 0; i < this->ns.size(); i++) {
      if (this->ns[i] != 0) {
        this->ns[i] /= static_cast<double>(DEPTH);
        this->n_new += this->ns[i];
      }
    }

    if (this->n_new == 0) {
      this->n_new = this->n_old;
    }

    for (uint32_t i = 0; i < this->ns.size(); i++) {
      this->dist_new[i] = this->ns[i] / this->n_new;
    }
    std::cout << std::endl;

    auto stop = std::chrono::high_resolution_clock::now();
    auto time =
        std::chrono::duration_cast<std::chrono::milliseconds>(stop - start);

    printf("[EM_WFCM - iter %2d] Compute time : %li\n", iter, time.count());
    printf("[EM_WFCM - iter %2d] Intermediate cardianlity : "
           "%9.1f\n\n",
           iter, this->n_new);
    iter++;

    /*print_stats();*/
  }

  void print_stats() {
    std::cout << "ns : " << std::endl;
    for (size_t i = 0; i < this->ns.size(); i++) {
      if (this->ns[i] != 0) {
        std::cout << i << ":" << this->ns[i] << " ";
      }
    }
    std::cout << std::endl;

    std::cout << "Dist_new: " << std::endl;
    for (uint32_t i = 0; i < this->dist_new.size(); i++) {
      if (this->dist_new[i] != 0) {
        std::cout << i << ":" << this->dist_new[i] << " ";
      }
    }
    std::cout << std::endl;
  }
};

uint32_t hashing(TUPLE tuple, uint32_t depth) {
  uint32_t crc = 0;
  std::cout << "Depth " << depth << " Hashing " << tuple << " ";
  if (depth == 0) {
    /*crc = 0xFFFFFFFF;*/
    crc = crc32(0L, Z_NULL, 0);
    std::cout << "Depth 0 initial crc " << crc << " ";
    crc = crc32(crc, tuple.num_array, tuple.sz);
    /*crc &= 0xFFFFFFFF;*/
  } else {
    crc = 0xF0000000;
    crc = crc32(crc, tuple.num_array, tuple.sz);
  }
  std::cout << "returning " << crc % W1 << std::endl;
  return crc % W1;
}

extern "C" {
void *EM_WFCM_new(uint32_t *s1_1, uint32_t *s1_2, uint32_t *s2_1,
                  uint32_t *s2_2, uint32_t *s3_1, uint32_t *s3_2, TUPLE *tuples,
                  uint32_t tuples_sz) {
  uint8_t data[4] = {0x12, 0x34, 0x56, 0x78}; // Same input as Python
  uint32_t crc = crc32(0, data, 4);           // Initial value is 0

  std::cout << "CRC32: " << crc % (8 * 8 * 8192) << std::endl;
  std::cout << "CRC32: " << crc % (W1) << std::endl;
  std::cout << "CRC32: " << crc % W1 << std::endl;

  std::cout << "[WaterfallFcm CTypes] Start parsing python to c" << std::endl;
  vector<uint32_t> stages_sz = {W1, W2, W3};
  vector<uint32_t> max_counts = {ADD_LEVEL1, ADD_LEVEL2,
                                 std::numeric_limits<uint32_t>::max()};
  vector<vector<vector<uint32_t>>> stages;
  std::cout << "[WaterfallFcm CTypes] Start resizing stages " << std::endl;
  stages.resize(DEPTH);
  stages[0].resize(NUM_STAGES);
  stages[1].resize(NUM_STAGES);

  std::cout << "[WaterfallFcm CTypes] Start resizing stages[d][s] "
            << std::endl;
  stages[0][0].resize(W1);
  stages[0][1].resize(W2);
  stages[0][2].resize(W3);
  stages[1][0].resize(W1);
  stages[1][1].resize(W2);
  stages[1][2].resize(W3);

  std::cout << "\tcopy stages" << std::endl;
  std::copy_n(s1_1, W1, stages[0][0].begin());
  std::copy_n(s2_1, W2, stages[0][1].begin());
  std::copy_n(s3_1, W3, stages[0][2].begin());
  std::copy_n(s1_2, W1, stages[1][0].begin());
  std::copy_n(s2_2, W2, stages[1][1].begin());
  std::copy_n(s3_2, W3, stages[1][2].begin());

  std::cout << "\tdone stages" << std::endl;

  // Setup tuple list
  std::cout << "[WaterfallFcm CTypes] Setup FlowTuple vector with size "
            << tuples_sz << std::endl;
  std::vector<TUPLE> tuples_vec(tuples_sz);
  for (size_t i = 0; i < tuples_sz; i++) {
    tuples_vec.at(i) = tuples[i];
    tuples_vec.at(i).sz = TupleSize::SrcTuple;
  }

  std::cout << "[WaterfallFcm CTypes] Checking vector with "
            << tuples_vec.size() << std::endl;
  for (size_t i = 0; i < tuples_vec.size(); i++) {
    std::cout << i << " : " << tuples_vec.at(i) << std::endl;
  }
  /*std::cout << std::endl;*/
  std::cout << "[WaterfallFCM] Calculate initial degrees from Waterfall..."
            << std::endl;
  vector<vector<uint32_t>> init_degree(DEPTH, vector<uint32_t>(W1));

  for (size_t d = 0; d < DEPTH; d++) {
    for (auto &tuple : tuples_vec) {
      uint32_t hash_idx = hashing(tuple, d);
      init_degree[d][hash_idx]++;
    }
  }
  std::cout << "[WaterfallFCM] ...done!" << std::endl;
  for (size_t d = 0; d < DEPTH; d++) {
    std::cout << "Depth " << d << std::endl;
    for (size_t i = 0; i < init_degree[d].size(); i++) {
      if (init_degree[d][i] == 0) {
        continue;
      }
      std::cout << i << ":" << init_degree[d][i] << " ";
    }
    std::cout << std::endl;
  }
  uint32_t max_counter_value = 0;
  vector<uint32_t> max_degree = {0, 0};
  // Summarize sketch and find collisions
  // depth, stage, idx, (count, total degree, sketch degree)
  vector<vector<vector<vector<uint32_t>>>> summary(NUM_STAGES);
  // Create virtual counters based on degree and count
  // depth, degree, count, value, collisions
  vector<vector<vector<uint32_t>>> virtual_counters;
  // Base for EM
  vector<vector<vector<uint32_t>>> sketch_degrees;
  vector<vector<uint32_t>> init_fsd(DEPTH);
  // depth, stage, idx, layer, vector<stage, local degree, total degree, min
  // value>
  vector<vector<vector<vector<vector<uint32_t>>>>> overflow_paths(NUM_STAGES);
  // depth, degree, count, value, vector<stage, local collisions, total
  // collisions, min value>
  vector<vector<vector<vector<vector<uint32_t>>>>> thresholds;

  std::cout << "[EM_WFCM] Setting up summary and overflow paths..."
            << std::endl;
  // Setup sizes for summary and overflow_paths
  for (size_t d = 0; d < DEPTH; d++) {
    summary[d].resize(NUM_STAGES);
    overflow_paths[d].resize(NUM_STAGES);
    for (size_t stage = 0; stage < NUM_STAGES; stage++) {
      summary[d][stage].resize(stages[d][stage].size(), vector<uint32_t>(3, 0));
      overflow_paths[d][stage].resize(stages[d][stage].size());
    }
  }
  std::cout << "[EM_WFCM] ...done!" << std::endl;
  std::cout << "[EM_WFCM] Setting up virtual counters and thresholds..."
            << std::endl;

  virtual_counters.resize(DEPTH);
  sketch_degrees.resize(DEPTH);
  thresholds.resize(DEPTH);

  std::cout << "[EM_WFCM] ...done!" << std::endl;
  std::cout << "[EM_WFCM] Load count from sketches into virtual counters and "
               "thresholds..."
            << std::endl;
  for (size_t d = 0; d < DEPTH; d++) {
    for (size_t stage = 0; stage < NUM_STAGES; stage++) {
      for (size_t i = 0; i < stages[d][stage].size(); i++) {
        if (stages[d][stage][i] <= 0) {
          continue;
        }

        // If overflown increase the minimal value for the collisions
        summary[d][stage][i][0] = stages[d][stage][i];
        if (stages[d][stage][i] >= max_counts[stage]) {
          summary[d][stage][i][0] = max_counts[stage] - 1;
        }

        // Store local and initial degree
        if (stage == 0) {
          summary[d][stage][i][1] = 1;
          summary[d][stage][i][2] = init_degree[d][i];
        } else if (stage > 0) { // Start checking childeren from stage 1 and up
          for (size_t k = 0; k < K; k++) {
            uint32_t child_idx = i * K + k;

            // Add childs count, total and sketch degree to current counter
            if (stages[d][stage - 1][child_idx] >= max_counts[stage - 1]) {
              summary[d][stage][i][0] += summary[d][stage - 1][child_idx][0];
              summary[d][stage][i][1] += summary[d][stage - 1][child_idx][1];
              summary[d][stage][i][2] += summary[d][stage - 1][child_idx][2];

              // Add overflow path of child to my own path
              for (auto &path : overflow_paths[d][stage - 1][child_idx]) {
                overflow_paths[d][stage][i].push_back(path);
              }
            }
          }
        }

        // If not overflown and non-zero, we are at the end of the path
        if (!(stages[d][stage][i] >= max_counts[stage]) &&
            summary[d][stage][i][0] > 0) {

          uint32_t count = summary[d][stage][i][0];
          uint32_t sketch_degree = summary[d][stage][i][1];
          uint32_t degree = summary[d][stage][i][2];
          max_counter_value = std::max(max_counter_value, count);
          max_degree[d] = std::max(max_degree[d], degree);

          if (max_counter_value >= init_fsd[d].size()) {
            init_fsd[d].resize(max_counter_value + 2);
          }

          if (degree == 1) {
            init_fsd[d][count]++;
          } else if (degree == count) {
            init_fsd[d][1] += count;
            /*} else if (degree + 1 == count) {*/
            /*  init_fsd[d][1] += (count - 1);*/
            /*  init_fsd[d][2] += 1;*/
          } else {
            if (degree >= virtual_counters[d].size()) {
              virtual_counters[d].resize(degree + 1);
              thresholds[d].resize(degree + 1);
              sketch_degrees[d].resize(degree + 1);
            }

            // Separate L1 VC's as these do not require thresholds to solve.
            // Store L1 VC in degree 0
            /*if (sketch_degree == 1) {*/
            /*  sketch_degree = degree;*/
            /*  degree = 0;*/
            /*}*/
            // Add entry to VC with its degree [1] and count [0]
            virtual_counters[d][degree].push_back(count);
            sketch_degrees[d][degree].push_back(sketch_degree);

            thresholds[d][degree].push_back(overflow_paths[d][stage][i]);
          }

        } else {
          uint32_t max_val = summary[d][stage][i][0];
          uint32_t local_degree = summary[d][stage][i][1];
          uint32_t total_degree = summary[d][stage][i][2];
          vector<uint32_t> local = {static_cast<uint32_t>(stage), local_degree,
                                    total_degree, max_val};
          overflow_paths[d][stage][i].push_back(local);
        }
      }
    }
  }

  std::cout << "[EM_WFCM] ...done!" << std::endl;

  if (1) {
    // Print vc with thresholds
    for (size_t d = 0; d < DEPTH; d++) {
      for (size_t st = 0; st < virtual_counters[d].size(); st++) {
        if (virtual_counters[d][st].size() == 0) {
          continue;
        }
        for (size_t i = 0; i < virtual_counters[d][st].size(); i++) {
          printf("Depth %zu, Degree %zu, Sketch Degree %u, Index %zu ]= Val "
                 "%d \tThresholds: ",
                 d, st, sketch_degrees[d][st][i], i,
                 virtual_counters[d][st][i]);
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
      for (size_t i = 0; i < init_fsd[d].size(); i++) {
        if (init_fsd[d][i] != 0) {
          printf("Depth %zu, Index %zu ]= Val %d\n", d, i, init_fsd[d][i]);
        }
      }
    }
  }

  std::cout << "Maximum degree is: " << max_degree[0] << ", " << max_degree[1]
            << std::endl;
  std::cout << "Maximum counter value is: " << max_counter_value << std::endl;

  std::cout << "[EMS_FSD] Initializing EMS_FSD..." << std::endl;
  return new EM_WFCM(thresholds, max_counter_value, max_degree,
                     virtual_counters, sketch_degrees, init_fsd);
}

void EM_WFCM_next_epoch(void *ptr) {
  EM_WFCM *em = reinterpret_cast<EM_WFCM *>(ptr);
  em->next_epoch();
}

vector<double> *get_ns(void *ptr) {
  EM_WFCM *em = reinterpret_cast<EM_WFCM *>(ptr);
  return &em->ns;
}

size_t vector_size(vector<double> *v) { return v->size(); }
double vector_get(vector<double> *v, size_t i) { return v->at(i); }
}

#endif
