#include "protocol.hpp"
#include <catch2/catch_all.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_predicate.hpp>
#include <catch2/matchers/catch_matchers_quantifiers.hpp>
#include <cstddef>
#include <unistd.h>

namespace am {

TEST_CASE("LinnearArray works", "[LinnearArray]") {
  using namespace Catch::Matchers;
  LinnearArray arr(100);

  REQUIRE(arr.size() == ::sysconf(_SC_PAGESIZE));
  REQUIRE(arr.at(0) == 'x');
  REQUIRE(arr.at(1) == 0);
  arr.at(0) = 0;
  auto vec = arr.to_vector();
  REQUIRE_THAT(vec, AllMatch(Predicate<char>([](char e) { return e == 0; },
                                             "equal to zero")));
}

TEST_CASE("RingBuffer works", "[RingBuffer]") {
  using namespace Catch::Matchers;

  size_t pagesize = ::sysconf(_SC_PAGESIZE);

  RingBuffer buf(100, 20000, 40000);
  REQUIRE(buf.prepared().size() == pagesize);
  buf.consume(pagesize);
  REQUIRE(buf.prepared().size() == 0);
  buf.commit(pagesize);
  REQUIRE(buf.prepared().size() == pagesize);
  buf.consume(pagesize / 2);
  buf.commit(pagesize / 4);
  auto prepared = buf.prepared();
  REQUIRE(prepared.size() == (pagesize / 2 + pagesize / 4));
  REQUIRE(prepared.count() == 2);
}

} // namespace am
