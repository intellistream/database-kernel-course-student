// Independent C++17 warm-up, not RMDB production code.
// Complete unpin() and can_evict(). Never make assertions pass by disabling them.
#include <cassert>
#include <stdexcept>
struct Frame { int pin_count=0; bool dirty=false; };
void pin(Frame& f) { ++f.pin_count; }
bool unpin(Frame& f, bool changed) {
  // TODO: reject underflow, merge dirty state, decrement pin count.
  throw std::logic_error("L0/L1 exercise: implement unpin");
}
bool can_evict(const Frame& f) {
  // TODO: encode the eviction invariant.
  throw std::logic_error("L0/L1 exercise: implement can_evict");
}
int main() {
  Frame f; pin(f); pin(f);
  assert(!can_evict(f)); assert(unpin(f,true)); assert(f.dirty);
  assert(!can_evict(f)); assert(unpin(f,false)); assert(f.dirty);
  assert(can_evict(f)); assert(!unpin(f,false)); assert(f.pin_count==0);
}
