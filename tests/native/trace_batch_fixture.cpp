#include "trace_output.hpp"
#include <limits>
bool ILLIXR::trace_output::transport_write(const char *data, std::size_t size) {
  return std::fwrite(data, 1, size, stdout) == size;
}
int main(int argc, char **) {
  using namespace ILLIXR::trace_output;
  if (argc > 1) begin();
  for (unsigned i = 0; i < 600; ++i) {
    print("ILLIXR_TEST {\"index\":%u,\"negative\":%lld,\"unsigned\":%llu,\"values\":[",
          i, std::numeric_limits<long long>::min(), std::numeric_limits<unsigned long long>::max());
    for (double v : {0., -0., 1e-12, 1e20, 1.2345678901234567, -3.141592653589793}) {
      print("%.17g,", v);
    }
    print("%.9g],\"status\":\"%s\",\"literal\":\"100%%\"}\n", 0.1234567890123, "valid");
  }
  return finish() ? 0 : 1;
}
