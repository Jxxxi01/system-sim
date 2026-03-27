#include "isa/assembler.hpp"
#include "isa/opcode.hpp"
#include "test_harness.hpp"

#include <array>
#include <stdexcept>
#include <string>

namespace {

void ExpectAssembleThrowsContains(const std::string& src, std::uint64_t base_va,
                                  const std::string& needle1, const std::string& needle2 = "") {
  try {
    (void)sim::isa::AssembleText(src, base_va);
    SIM_EXPECT_TRUE(false);
  } catch (const std::runtime_error& ex) {
    const std::string msg = ex.what();
    SIM_EXPECT_TRUE(msg.find(needle1) != std::string::npos);
    if (!needle2.empty()) {
      SIM_EXPECT_TRUE(msg.find(needle2) != std::string::npos);
    }
  }
}

}  // namespace

SIM_TEST(AssembleProgramWithLabels_Succeeds) {
  const std::string src = R"(
start:
  LI x1, 5
  LI x2, 7
loop: ADD x3, x1, x2
  BEQ x3, x0, end
  J loop
end:
  HALT
)";

  const sim::isa::AsmProgram program = sim::isa::AssembleText(src, 0x1000);
  SIM_EXPECT_EQ(program.base_va, 0x1000u);
  SIM_EXPECT_EQ(program.code.size(), static_cast<std::size_t>(6));
  SIM_EXPECT_EQ(program.code[2].op, sim::isa::Op::ADD);
  SIM_EXPECT_EQ(program.code[2].rd, 3);
  SIM_EXPECT_EQ(program.code[2].rs1, 1);
  SIM_EXPECT_EQ(program.code[2].rs2, 2);
  SIM_EXPECT_EQ(program.code[3].imm, 4);    // BEQ -> end
  SIM_EXPECT_EQ(program.code[4].imm, -12);  // J -> loop

  const auto located = sim::isa::ToLocated(program);
  SIM_EXPECT_EQ(located.size(), program.code.size());
  SIM_EXPECT_EQ(located[0].va, 0x1000u);
  SIM_EXPECT_EQ(located[5].va, 0x1014u);
}

SIM_TEST(PcRelativeResolution_NextPcConvention_Correct) {
  const std::string src = R"(
start:
  J forward
back:
  BEQ x1, x2, back
forward:
  CALL back
  HALT
)";

  const sim::isa::AsmProgram program = sim::isa::AssembleText(src, 0x3000);
  SIM_EXPECT_EQ(program.code.size(), static_cast<std::size_t>(4));
  SIM_EXPECT_EQ(program.code[0].op, sim::isa::Op::J);
  SIM_EXPECT_EQ(program.code[0].imm, 4);    // 0x3000 -> next 0x3004 -> target 0x3008
  SIM_EXPECT_EQ(program.code[1].op, sim::isa::Op::BEQ);
  SIM_EXPECT_EQ(program.code[1].imm, -4);   // 0x3004 -> next 0x3008 -> target 0x3004
  SIM_EXPECT_EQ(program.code[2].op, sim::isa::Op::CALL);
  SIM_EXPECT_EQ(program.code[2].imm, -8);   // 0x3008 -> next 0x300C -> target 0x3004
}

SIM_TEST(InstructionFields_AfterParse_Correct) {
  const std::string src = R"(
  LI x1, -9
  ADD x2, x1, x1
  XOR x3, x1, x2
  LD x5, [x2-8]
  ST x6, [ x3 ]
  SYSCALL 42
  RET
  HALT
)";

  const sim::isa::AsmProgram program = sim::isa::AssembleText(src, 0x2000);
  SIM_EXPECT_EQ(program.code.size(), static_cast<std::size_t>(8));

  SIM_EXPECT_EQ(program.code[0].op, sim::isa::Op::LI);
  SIM_EXPECT_EQ(program.code[0].rd, 1);
  SIM_EXPECT_EQ(program.code[0].imm, -9);

  SIM_EXPECT_EQ(program.code[3].op, sim::isa::Op::LD);
  SIM_EXPECT_EQ(program.code[3].rd, 5);
  SIM_EXPECT_EQ(program.code[3].rs1, 2);
  SIM_EXPECT_EQ(program.code[3].imm, -8);

  SIM_EXPECT_EQ(program.code[4].op, sim::isa::Op::ST);
  SIM_EXPECT_EQ(program.code[4].rs2, 6);
  SIM_EXPECT_EQ(program.code[4].rs1, 3);
  SIM_EXPECT_EQ(program.code[4].imm, 0);

  SIM_EXPECT_EQ(program.code[5].op, sim::isa::Op::SYSCALL);
  SIM_EXPECT_EQ(program.code[5].imm, 42);

  SIM_EXPECT_EQ(program.code[6].op, sim::isa::Op::RET);
  SIM_EXPECT_EQ(program.code[7].op, sim::isa::Op::HALT);
}

SIM_TEST(UndefinedLabel_Fails) {
  const std::string src = R"(
J missing_label
)";
  ExpectAssembleThrowsContains(src, 0x1000, "undefined label", "asm:2:");
}

SIM_TEST(CommentsAndWhitespace_Succeeds) {
  const std::string src = R"(
# comment line
entry:
   li x1, 1      # init

  ADD x2, x1, x1
  ld x3, [ x2 + 0 ]
st x3,[x2-4]
  j 8
  HALT # done
)";
  const sim::isa::AsmProgram program = sim::isa::AssembleText(src, 0x1800);
  SIM_EXPECT_EQ(program.code.size(), static_cast<std::size_t>(6));
  SIM_EXPECT_EQ(program.code[0].op, sim::isa::Op::LI);
  SIM_EXPECT_EQ(program.code[2].op, sim::isa::Op::LD);
  SIM_EXPECT_EQ(program.code[2].imm, 0);
  SIM_EXPECT_EQ(program.code[3].op, sim::isa::Op::ST);
  SIM_EXPECT_EQ(program.code[3].imm, -4);
  SIM_EXPECT_EQ(program.code[4].op, sim::isa::Op::J);
  SIM_EXPECT_EQ(program.code[4].imm, 8);  // numeric target: direct pc-relative offset
}

SIM_TEST(NumericTargetIsPcRelativeOffset_Succeeds) {
  const std::string src = R"(
  J 16
  BEQ x1, x2, -12
  CALL 4
  HALT
)";
  const sim::isa::AsmProgram program = sim::isa::AssembleText(src, 0x2400);
  SIM_EXPECT_EQ(program.code[0].imm, 16);
  SIM_EXPECT_EQ(program.code[1].imm, -12);
  SIM_EXPECT_EQ(program.code[2].imm, 4);
}

SIM_TEST(OpToString_AllOpcodes_AreStable) {
  struct OpStringCase {
    sim::isa::Op op;
    const char* expected;
  };

  const std::array<OpStringCase, 12> cases = {{
      {sim::isa::Op::NOP, "NOP"},
      {sim::isa::Op::LI, "LI"},
      {sim::isa::Op::ADD, "ADD"},
      {sim::isa::Op::XOR, "XOR"},
      {sim::isa::Op::LD, "LD"},
      {sim::isa::Op::ST, "ST"},
      {sim::isa::Op::J, "J"},
      {sim::isa::Op::BEQ, "BEQ"},
      {sim::isa::Op::CALL, "CALL"},
      {sim::isa::Op::RET, "RET"},
      {sim::isa::Op::HALT, "HALT"},
      {sim::isa::Op::SYSCALL, "SYSCALL"},
  }};

  for (const OpStringCase& test_case : cases) {
    const std::string actual = sim::isa::OpToString(test_case.op);
    SIM_EXPECT_EQ(actual, std::string(test_case.expected));
    SIM_EXPECT_TRUE(actual != "UNKNOWN");
  }
}

SIM_TEST(OpcodeCaseInsensitive_LabelAndRegCaseSensitive_BehaviorLocked) {
  const std::string mixed_opcode = R"(
Loop:
  Add x1, x2, x3
  j Loop
)";
  const sim::isa::AsmProgram program = sim::isa::AssembleText(mixed_opcode, 0x1000);
  SIM_EXPECT_EQ(program.code[0].op, sim::isa::Op::ADD);
  SIM_EXPECT_EQ(program.code[1].op, sim::isa::Op::J);
  SIM_EXPECT_EQ(program.code[1].imm, -8);

  const std::string bad_reg = R"(
  LI X1, 5
)";
  ExpectAssembleThrowsContains(bad_reg, 0x1000, "bad register");

  const std::string label_case_sensitive = R"(
Loop:
  J loop
)";
  ExpectAssembleThrowsContains(label_case_sensitive, 0x1000, "undefined label");
}

SIM_TEST(InstrVa_OutOfRange_Throws) {
  const std::string src = R"(
  NOP
)";
  const sim::isa::AsmProgram program = sim::isa::AssembleText(src, 0x1000);
  SIM_EXPECT_EQ(sim::isa::InstrVa(program, 0), 0x1000u);
  try {
    (void)sim::isa::InstrVa(program, 1);
    SIM_EXPECT_TRUE(false);
  } catch (const std::out_of_range&) {
    SIM_EXPECT_TRUE(true);
  }
}

SIM_TEST(BadMemOperand_FailsWithKind) {
  const std::string src = R"(
  LD x1, x2+8
)";
  ExpectAssembleThrowsContains(src, 0x1000, "bad mem operand", "asm:2:");
}

int main() {
  return sim::test::RunAll();
}
