#include "core/scaffold.hpp"
#include "isa/scaffold.hpp"
#include "security/scaffold.hpp"
#include "test_harness.hpp"

SIM_TEST(Sanity_ConstantsAreDefined) {
  SIM_EXPECT_TRUE(sim::core::kScaffoldName != nullptr);
  SIM_EXPECT_EQ(sim::isa::kWordSizeBytes, 4);
  SIM_EXPECT_EQ(static_cast<sim::security::ContextHandle>(0), 0u);
}

int main() {
  return sim::test::RunAll();
}
