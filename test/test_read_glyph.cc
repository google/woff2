// Tests for issue #191: unbounded ReadGlyph allocation fix.
//
// Exercises src/glyph.cc's ReadGlyph directly by building the simple-glyph
// byte layout per the OpenType sfnt spec and asserting the contract described
// in PLAN.md section 4 (edge cases table).
//
// The simple-glyph payload layout targeted here (what ReadGlyph receives):
//   int16  numberOfContours         (must be > 0 for the simple-glyph branch)
//   int16  xMin, yMin, xMax, yMax
//   uint16 endPtsOfContours[numberOfContours]
//   uint16 instructionLength
//   uint8  instructions[instructionLength]
//   uint8  flags[variable, one per point, with optional repeat]
//   uint8/int16 xCoordinates[variable]
//   uint8/int16 yCoordinates[variable]
//
// Each test builds just enough bytes to reach the behaviour under test and
// asserts ReadGlyph's return value (and, on success, the per-contour sizes).

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "glyph.h"

namespace {

// Big-endian byte appenders ---------------------------------------------------

void PutU8(std::vector<uint8_t>& b, uint8_t v) { b.push_back(v); }
void PutU16(std::vector<uint8_t>& b, uint16_t v) {
  b.push_back(static_cast<uint8_t>(v >> 8));
  b.push_back(static_cast<uint8_t>(v & 0xff));
}
void PutI16(std::vector<uint8_t>& b, int16_t v) {
  PutU16(b, static_cast<uint16_t>(v));
}

// Builds the byte layout of a simple glyph record.
// endPts is the sequence of endPtsOfContours values (one per contour).
// If fill_points is true, the function appends minimal flags/x/y so that the
// rest of ReadGlyph can parse the glyph to completion (useful for the valid
// baseline tests).  Each point costs 3 bytes (flag 0x37 + 1 byte x + 1 byte y).
// If fill_points is false, no flag/coord bytes are written; this is the right
// choice for "expected to be rejected before reaching point parsing" tests.
std::vector<uint8_t> BuildSimpleGlyph(const std::vector<uint16_t>& endPts,
                                      bool fill_points,
                                      size_t trailing_padding = 0) {
  std::vector<uint8_t> out;
  PutI16(out, static_cast<int16_t>(endPts.size()));  // numberOfContours
  PutI16(out, 0);   // xMin
  PutI16(out, 0);   // yMin
  PutI16(out, 10);  // xMax
  PutI16(out, 10);  // yMax
  for (uint16_t ep : endPts) PutU16(out, ep);
  PutU16(out, 0);   // instructionLength
  if (fill_points) {
    // Total point count per the sfnt spec: endPts.back() + 1.  Each point uses
    // a simple flag = 0x37 (on-curve, x & y are 1-byte positive shorts).
    size_t n_points = 0;
    if (!endPts.empty()) n_points = static_cast<size_t>(endPts.back()) + 1;
    for (size_t i = 0; i < n_points; ++i) PutU8(out, 0x37);
    for (size_t i = 0; i < n_points; ++i) PutU8(out, 1);  // x coord
    for (size_t i = 0; i < n_points; ++i) PutU8(out, 1);  // y coord
  }
  for (size_t i = 0; i < trailing_padding; ++i) PutU8(out, 0);
  return out;
}

void AppendRepeatedSamePointFlags(size_t n_points, std::vector<uint8_t>* out) {
  const uint8_t kSamePointFlag = 0x01 | 0x10 | 0x20;
  while (n_points > 0) {
    size_t run = n_points > 256 ? 256 : n_points;
    if (run == 1) {
      PutU8(*out, kSamePointFlag);
    } else {
      PutU8(*out, kSamePointFlag | 0x08);
      PutU8(*out, static_cast<uint8_t>(run - 1));
    }
    n_points -= run;
  }
}

// Test runner scaffolding -----------------------------------------------------

int g_total = 0;
int g_failed = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::fprintf(stderr, "  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
      ++g_failed;                                                              \
      return;                                                                  \
    }                                                                          \
  } while (0)

#define RUN(fn)                                                                \
  do {                                                                         \
    ++g_total;                                                                 \
    std::fprintf(stderr, "[RUN ] %s\n", #fn);                                  \
    int before = g_failed;                                                     \
    fn();                                                                      \
    if (g_failed == before) std::fprintf(stderr, "[ OK ] %s\n", #fn);          \
  } while (0)

// Tests -----------------------------------------------------------------------

// Baseline: single-contour glyph with three points parses correctly.
void Test_Simple_SingleContour_Valid() {
  auto bytes = BuildSimpleGlyph({2}, /*fill_points=*/true);
  woff2::Glyph g;
  bool ok = woff2::ReadGlyph(bytes.data(), bytes.size(), &g);
  CHECK(ok);
  CHECK(g.contours.size() == 1);
  CHECK(g.contours[0].size() == 3);
}

// Spec-clean monotonic endpoints [3, 7, 11] -> contour sizes [4, 4, 4].
void Test_Simple_Monotonic_Endpoints() {
  auto bytes = BuildSimpleGlyph({3, 7, 11}, /*fill_points=*/true);
  woff2::Glyph g;
  bool ok = woff2::ReadGlyph(bytes.data(), bytes.size(), &g);
  CHECK(ok);
  CHECK(g.contours.size() == 3);
  CHECK(g.contours[0].size() == 4);
  CHECK(g.contours[1].size() == 4);
  CHECK(g.contours[2].size() == 4);
}

// Equal endpoints are tolerated (PLAN.md explicitly says do NOT tighten < to <=).
// endPts [3, 3, 5] -> sizes [4, 0, 2].
void Test_Simple_Equal_Endpoints_Tolerated() {
  auto bytes = BuildSimpleGlyph({3, 3, 5}, /*fill_points=*/true);
  woff2::Glyph g;
  bool ok = woff2::ReadGlyph(bytes.data(), bytes.size(), &g);
  CHECK(ok);
  CHECK(g.contours.size() == 3);
  CHECK(g.contours[0].size() == 4);
  CHECK(g.contours[1].size() == 0);
  CHECK(g.contours[2].size() == 2);
}

// First contour with point_index == 0 is well-defined: num_points = 1
// because of the (i == 0 ? 1 : 0) addend.
void Test_Simple_FirstContour_PointIndexZero() {
  auto bytes = BuildSimpleGlyph({0}, /*fill_points=*/true);
  woff2::Glyph g;
  bool ok = woff2::ReadGlyph(bytes.data(), bytes.size(), &g);
  CHECK(ok);
  CHECK(g.contours.size() == 1);
  CHECK(g.contours[0].size() == 1);
}

// Upper bound on per-contour points that any *legitimate* post-rejection
// glyph should have touched.  The bug's allocation land at ~65533 points;
// any fix that prevents the wraparound will leave the contour untouched
// (or with a small legitimate value).
constexpr size_t kSaneContourCap = 1000;

// THE BUG FIX: two contours with endPts [5, 2] -> without the fix the
// subtraction wraps to 65533 and resize allocates ~MB of Points. With the
// fix, ReadGlyph must return false AND must not have resize()d contours[1]
// to the wrapped value first.
void Test_Simple_NonMonotonic_Rejected() {
  auto bytes = BuildSimpleGlyph({5, 2}, /*fill_points=*/false,
                                /*trailing_padding=*/4);
  woff2::Glyph g;
  bool ok = woff2::ReadGlyph(bytes.data(), bytes.size(), &g);
  CHECK(!ok);
  // Fix (a) requirement: the wrapped-size allocation must NOT have happened.
  if (g.contours.size() > 1) {
    CHECK(g.contours[1].size() < kSaneContourCap);
  }
}

// Minimal wrap case: endPts [1, 0] -> wrapped num_points = 65535 on contour 1.
// Without fix this is the worst-per-glyph allocation.  Must be rejected.
void Test_Simple_NonMonotonic_OneStepDown_Rejected() {
  auto bytes = BuildSimpleGlyph({1, 0}, /*fill_points=*/false,
                                /*trailing_padding=*/4);
  woff2::Glyph g;
  bool ok = woff2::ReadGlyph(bytes.data(), bytes.size(), &g);
  CHECK(!ok);
  if (g.contours.size() > 1) {
    CHECK(g.contours[1].size() < kSaneContourCap);
  }
}

// Non-monotonic from a very large value to zero: [0xFFFE, 0].
// Must be rejected; no giant allocation on contour 1 even though the wrap
// lands at num_points == 2 here (this one wouldn't have shown up as a DoS,
// but the monotonicity rule still applies per the sfnt spec).
void Test_Simple_NonMonotonic_LargeToZero_Rejected() {
  auto bytes = BuildSimpleGlyph({0xFFFE, 0}, /*fill_points=*/false,
                                /*trailing_padding=*/4);
  woff2::Glyph g;
  bool ok = woff2::ReadGlyph(bytes.data(), bytes.size(), &g);
  CHECK(!ok);
}

// Three contours, non-monotonic at the third slot: [3, 7, 4].  Must be
// rejected; contours[2] must not be grown to the wrapped size 65533.
void Test_Simple_NonMonotonic_AtLaterContour_Rejected() {
  auto bytes = BuildSimpleGlyph({3, 7, 4}, /*fill_points=*/false,
                                /*trailing_padding=*/4);
  woff2::Glyph g;
  bool ok = woff2::ReadGlyph(bytes.data(), bytes.size(), &g);
  CHECK(!ok);
  if (g.contours.size() > 2) {
    CHECK(g.contours[2].size() < kSaneContourCap);
  }
}

// Flags are run-length encoded, so many logical points may be represented by
// far fewer flag bytes. This guards against reintroducing a remaining-bytes
// bound that assumes one stored flag byte per point.
void Test_Simple_RleFlags_CanRepresentManyPoints() {
  std::vector<uint8_t> bytes;
  PutI16(bytes, 1);
  PutI16(bytes, 0); PutI16(bytes, 0); PutI16(bytes, 10); PutI16(bytes, 10);
  PutU16(bytes, 1000);  // 1001 logical points.
  PutU16(bytes, 0);     // instructionLength
  AppendRepeatedSamePointFlags(1001, &bytes);
  woff2::Glyph g;
  bool ok = woff2::ReadGlyph(bytes.data(), bytes.size(), &g);
  CHECK(ok);
  CHECK(g.contours.size() == 1);
  CHECK(g.contours[0].size() == 1001);
}

// Scaled-up reproducer of the reported DoS: many contours where every odd
// contour has a point_index below its predecessor.  Without the fix this
// would try to resize() many vectors to ~65535 points each.  With the fix
// the first non-monotonic transition is rejected outright -- aggregate
// allocation across contours must stay tiny.
void Test_Simple_DoS_Reproducer_ManyContours() {
  std::vector<uint16_t> endPts;
  endPts.reserve(200);
  for (int i = 0; i < 100; ++i) {
    endPts.push_back(100);
    endPts.push_back(0);  // wraps on every even-indexed contour (i>0)
  }
  auto bytes = BuildSimpleGlyph(endPts, /*fill_points=*/false,
                                /*trailing_padding=*/4);
  woff2::Glyph g;
  bool ok = woff2::ReadGlyph(bytes.data(), bytes.size(), &g);
  CHECK(!ok);
  size_t total_points = 0;
  for (const auto& c : g.contours) total_points += c.size();
  // Spec-legal rejection leaves at most ~100 points allocated on the very
  // first contour; the wrap would produce >1M if the fix regressed.
  CHECK(total_points < 10000);
}

// numberOfContours == 0 denotes an empty glyph.  PLAN.md marks the behaviour
// as "early return at line 84 | unchanged".  We don't assume a specific
// return value, we only assert the call doesn't crash and doesn't allocate
// spurious contours on success.
void Test_Simple_ZeroContours_NoCrash() {
  // Minimum payload: just the 10-byte bbox header, no endPts.
  std::vector<uint8_t> bytes;
  PutI16(bytes, 0);
  PutI16(bytes, 0); PutI16(bytes, 0); PutI16(bytes, 0); PutI16(bytes, 0);
  woff2::Glyph g;
  bool ok = woff2::ReadGlyph(bytes.data(), bytes.size(), &g);
  // Either outcome is acceptable per PLAN.md; on success, no simple contours.
  if (ok) CHECK(g.contours.empty());
  (void)ok;
}

// numberOfContours < -1 must be rejected (PLAN.md section 4 row 3).
void Test_NumContours_LessThanNegativeOne_Rejected() {
  std::vector<uint8_t> bytes;
  PutI16(bytes, -2);
  PutI16(bytes, 0); PutI16(bytes, 0); PutI16(bytes, 0); PutI16(bytes, 0);
  // Add plenty of padding in case the code reads further before deciding.
  for (int i = 0; i < 32; ++i) PutU8(bytes, 0);
  woff2::Glyph g;
  bool ok = woff2::ReadGlyph(bytes.data(), bytes.size(), &g);
  CHECK(!ok);
}

// Truncated input: simple glyph header promises 5 contours but the buffer
// only contains 2 endPts.  Must be rejected by the existing ReadU16 guard.
void Test_Simple_Truncated_Endpts_Rejected() {
  std::vector<uint8_t> bytes;
  PutI16(bytes, 5);  // numberOfContours
  PutI16(bytes, 0); PutI16(bytes, 0); PutI16(bytes, 0); PutI16(bytes, 0);
  PutU16(bytes, 1);  // endPts[0]
  PutU16(bytes, 2);  // endPts[1] — only 2 of 5 present
  woff2::Glyph g;
  bool ok = woff2::ReadGlyph(bytes.data(), bytes.size(), &g);
  CHECK(!ok);
}

// Input so short we can't even read the numberOfContours field.
void Test_TooShort_ForHeader_Rejected() {
  std::vector<uint8_t> bytes = {0x00};
  woff2::Glyph g;
  bool ok = woff2::ReadGlyph(bytes.data(), bytes.size(), &g);
  CHECK(!ok);
}

// len == 0: empty input.  Must not crash; must return false.
void Test_EmptyInput_Rejected() {
  woff2::Glyph g;
  // Passing nullptr would be UB; use a non-null, zero-length buffer.
  uint8_t stub = 0;
  bool ok = woff2::ReadGlyph(&stub, 0, &g);
  CHECK(!ok);
}

// Two contours with equal endpoints at zero: [0, 0].  Monotonic check must
// NOT trigger (point_index == last_point_index is allowed).  Contour sizes
// are [1, 0] (the +1 applies only to i==0).
void Test_Simple_TwoContours_BothZero_Accepted() {
  auto bytes = BuildSimpleGlyph({0, 0}, /*fill_points=*/true);
  woff2::Glyph g;
  bool ok = woff2::ReadGlyph(bytes.data(), bytes.size(), &g);
  CHECK(ok);
  CHECK(g.contours.size() == 2);
  CHECK(g.contours[0].size() == 1);
  CHECK(g.contours[1].size() == 0);
}

// Boundary: endPts[i] one below last_point_index -- the smallest possible
// violation.  [5, 4] must be rejected; without the fix num_points wraps to
// 65535 and contours[1] would grow to 65535 entries.
void Test_Simple_NonMonotonic_OneUnderPrev_Rejected() {
  auto bytes = BuildSimpleGlyph({5, 4}, /*fill_points=*/false,
                                /*trailing_padding=*/4);
  woff2::Glyph g;
  bool ok = woff2::ReadGlyph(bytes.data(), bytes.size(), &g);
  CHECK(!ok);
  if (g.contours.size() > 1) {
    CHECK(g.contours[1].size() < kSaneContourCap);
  }
}

// Strictly-increasing-by-one endpoints: [0, 1, 2, 3].  Every contour has
// exactly one point (sizes [1, 1, 1, 1]).
void Test_Simple_IncrementByOne() {
  auto bytes = BuildSimpleGlyph({0, 1, 2, 3}, /*fill_points=*/true);
  woff2::Glyph g;
  bool ok = woff2::ReadGlyph(bytes.data(), bytes.size(), &g);
  CHECK(ok);
  CHECK(g.contours.size() == 4);
  for (size_t i = 0; i < 4; ++i) CHECK(g.contours[i].size() == 1);
}

}  // namespace

int main() {
  RUN(Test_Simple_SingleContour_Valid);
  RUN(Test_Simple_Monotonic_Endpoints);
  RUN(Test_Simple_Equal_Endpoints_Tolerated);
  RUN(Test_Simple_FirstContour_PointIndexZero);
  RUN(Test_Simple_NonMonotonic_Rejected);
  RUN(Test_Simple_NonMonotonic_OneStepDown_Rejected);
  RUN(Test_Simple_NonMonotonic_LargeToZero_Rejected);
  RUN(Test_Simple_NonMonotonic_AtLaterContour_Rejected);
  RUN(Test_Simple_RleFlags_CanRepresentManyPoints);
  RUN(Test_Simple_DoS_Reproducer_ManyContours);
  RUN(Test_Simple_ZeroContours_NoCrash);
  RUN(Test_NumContours_LessThanNegativeOne_Rejected);
  RUN(Test_Simple_Truncated_Endpts_Rejected);
  RUN(Test_TooShort_ForHeader_Rejected);
  RUN(Test_EmptyInput_Rejected);
  RUN(Test_Simple_TwoContours_BothZero_Accepted);
  RUN(Test_Simple_NonMonotonic_OneUnderPrev_Rejected);
  RUN(Test_Simple_IncrementByOne);

  std::fprintf(stderr, "\n%d/%d tests passed\n", g_total - g_failed, g_total);
  return g_failed == 0 ? 0 : 1;
}
