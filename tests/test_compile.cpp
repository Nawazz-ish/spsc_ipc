#include "../shared/include/spsc_queue.hpp"
#include <cstdint>

struct Msg { uint64_t a, b; uint8_t pad[48]; };
static_assert(sizeof(Msg) == 64);

int main() {
    SPSCQueue<Msg, 1024> q;
    Msg m{}, out;
    q.try_push(m);
    q.try_pop(out);
    return 0;
}
