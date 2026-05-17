#include "../shared/include/tsc.hpp"
#include <cstdio>

int main() {
    printf("Calibrating TSC frequency (5 runs of 100ms each)...\n");
    for (int i = 0; i < 5; ++i) {
        auto cal = calibrate_tsc();
        printf("  run %d: %.6f ticks/ns  (~%.3f GHz)\n",
               i, cal.ticks_per_ns, cal.ticks_per_ns);
    }
    return 0;
}
