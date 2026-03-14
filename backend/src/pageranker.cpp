#include "pagerank.h"
#include <cstdint>

PageRank::PageRank(double damping,
                   int iterations)
    : damping_(damping),
      iterations_(iterations) {}

std::vector<double>
PageRank::compute(
    const std::vector<std::vector<uint32_t>>& graph) {

    size_t N = graph.size();

    std::vector<double> rank(N, 1.0 / N);
    std::vector<double> new_rank(N);

    for (int iter = 0; iter < iterations_; ++iter) {

        std::fill(new_rank.begin(),
                  new_rank.end(),
                  (1.0 - damping_) / N);

        for (size_t i = 0; i < N; ++i) {

            if (graph[i].empty())
                continue;

            double share =
                rank[i] / graph[i].size();

            for (uint32_t neighbor : graph[i]) {
                new_rank[neighbor] +=
                    damping_ * share;
            }
        }

        rank = new_rank;
    }

    return rank;
}