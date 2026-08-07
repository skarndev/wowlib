/* Driver for the Mach-O literal-atom probe; see cmake/DarwinAtomProbe.cmake.
 * Exits 0 only if both TUs' literals survived weak coalescing intact.
 * Built with the CXX compiler (that is what carries the shim flags), so the
 * assembly symbols need C linkage. */
#include <cstring>

extern "C" const char *probe_a();
extern "C" const char *probe_b();

int main()
{
  bool ok = std::memcmp(probe_a(), "0123456789abcdef", 16) == 0
         && std::memcmp(probe_b(), "ZZZZZZZZZZZZZZZZ", 16) == 0;
  return ok ? 0 : 1;
}
