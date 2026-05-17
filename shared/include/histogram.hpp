#pragma once


#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>


class Histogram{
    std::vector<int64_t> samples_;

    public:
    void reserve(size_t n){ samples_.reserve(n);}

    void record(int64_t value) {samples_.push_back(value);}//with reserved capacity this is one store + one counter increment - about 2 ns

    size_t size() const {return samples_.size();}

    void report(const char* label){
        if(samples_.empty()){
            printf("%-20s: (empty)\n", label);
            return;
        }
        std::sort(samples_.begin(), samples_.end());// Sort once. For 1M samples this is ~50 ms — only done at report time, not on the hot path.
        auto pct = [&](double p){
            return samples_[size_t(samples_.size()*p)];
        };// Lambda to get the p-th percentile value. size_t index = floor(p * N), where N is the number of samples. For p=0.5 and N=100, this gives index 50 (the 51st sample in 0-based indexing). This is a common way to compute percentiles from a sorted list.
        printf("%-20s: p50=%6.2f ns, p90=%6.2f ns, p99=%6.2f ns, max=%6.2f ns\n",
            label,
            double(pct(0.5)),
            double(pct(0.9)),
            double(pct(0.99)),
            double(samples_.back()));
    }
};