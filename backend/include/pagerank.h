#pragma once
#include <cstdint>
#include <vector>

class PageRank {
public:
    PageRank(double damping = 0.85,
             int iterations = 30);

    std::vector<double> compute(
        const std::vector<std::vector<uint32_t>>& graph);

private:
    double damping_;
    int iterations_;
};