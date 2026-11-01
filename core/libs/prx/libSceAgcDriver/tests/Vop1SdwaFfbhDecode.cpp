#include "RdnaDecoder/RdnaInstructionDecoder.hpp"
#include <array>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <span>
#include <stdexcept>
#include <string>

using namespace ShaderRecompiler;

namespace {

constexpr std::uint32_t SourceVector = 4;
constexpr std::uint32_t DestinationVector = 3;

constexpr std::uint32_t Vop1SdwaWord(std::uint32_t opcode, std::uint32_t vdst) {
    return (0x3fu << 25u) | (vdst << 17u) | (opcode << 9u) | 0xf9u;
}

constexpr std::uint32_t Modifier(std::uint32_t destinationSelector, std::uint32_t unused, std::uint32_t sourceSelector, std::uint32_t signExtend) {
    return SourceVector | (destinationSelector << 8u) | (unused << 11u) | (sourceSelector << 16u) | (signExtend << 19u);
}

bool Matches(const RdnaInstruction& instruction, RdnaOpcode expectedOp, std::uint32_t unused, std::uint32_t sourceSelector, std::uint32_t signExtend) {
    return instruction.op == expectedOp && instruction.wordCount == 2u && instruction.sourceCount == 1u &&
        instruction.destination.kind == RdnaOperandKind::VectorRegister && instruction.destination.reg == DestinationVector &&
        instruction.destination.explicitSdwaDst && instruction.destination.sdwaSel == 6u &&
        instruction.destination.sdwaDstUnused == unused &&
        instruction.source0.kind == RdnaOperandKind::VectorRegister && instruction.source0.reg == SourceVector &&
        instruction.source0.sdwaSel == sourceSelector && instruction.source0.sdwaSext == (signExtend != 0u) &&
        !instruction.source0.negate && !instruction.source0.absolute;
}

struct RejectedCase {
    const char* name;
    std::uint32_t modifier;
};

}

int main() {
    int failures = 0;
    const std::uint32_t ffbhI32Word = Vop1SdwaWord(0x3bu, DestinationVector);
    const std::uint32_t ffbhU32Word = Vop1SdwaWord(0x39u, DestinationVector);
    const std::uint32_t ffblB32Word = Vop1SdwaWord(0x3au, DestinationVector);

    for (std::uint32_t sourceSelector = 0; sourceSelector < 7u; ++sourceSelector) {
        for (std::uint32_t signExtend = 0; signExtend < 2u; ++signExtend) {
            for (std::uint32_t unused = 0; unused < 3u; ++unused) {
                const std::array<std::uint32_t, 2> words{ffbhI32Word, Modifier(6u, unused, sourceSelector, signExtend)};
                try {
                    const auto instruction = DecodeRdnaInstruction(0u, words, 0u);
                    if (!Matches(instruction, RdnaOpcode::VFfbhI32, unused, sourceSelector, signExtend)) {
                        std::fprintf(stderr, "v_ffbh_i32 sdwa src0_sel %u sext %u dst_unused %u decodes with wrong operands\n", sourceSelector, signExtend, unused);
                        ++failures;
                    }
                } catch (const std::exception& error) {
                    std::fprintf(stderr, "v_ffbh_i32 sdwa src0_sel %u sext %u dst_unused %u: %s\n", sourceSelector, signExtend, unused, error.what());
                    ++failures;
                }
            }
        }
    }

    try {
        const std::array<std::uint32_t, 2> u32Words{ffbhU32Word, Modifier(6u, 0u, 1u, 0u)};
        const auto u32Instruction = DecodeRdnaInstruction(0u, u32Words, 0u);
        if (!Matches(u32Instruction, RdnaOpcode::VFfbhU32, 0u, 1u, 0u)) {
            std::fprintf(stderr, "v_ffbh_u32 sdwa decodes with wrong operands\n");
            ++failures;
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "v_ffbh_u32 sdwa: %s\n", error.what());
        ++failures;
    }

    try {
        const std::array<std::uint32_t, 2> blWords{ffblB32Word, Modifier(6u, 1u, 2u, 1u)};
        const auto blInstruction = DecodeRdnaInstruction(0u, blWords, 0u);
        if (!Matches(blInstruction, RdnaOpcode::VFfblB32, 1u, 2u, 1u)) {
            std::fprintf(stderr, "v_ffbl_b32 sdwa decodes with wrong operands\n");
            ++failures;
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "v_ffbl_b32 sdwa: %s\n", error.what());
        ++failures;
    }

    constexpr std::array<RejectedCase, 7> rejectedCases{{
        {"partial byte destination selector", Modifier(0u, 0u, 0u, 0u)},
        {"partial word destination selector", Modifier(4u, 0u, 0u, 0u)},
        {"reserved dst_unused", Modifier(6u, 3u, 0u, 0u)},
        {"reserved dst_sel", Modifier(7u, 0u, 0u, 0u)},
        {"reserved src0_sel", Modifier(6u, 0u, 7u, 0u)},
        {"neg modifier on integer source", Modifier(6u, 0u, 0u, 0u) | (1u << 20u)},
        {"abs modifier on integer source", Modifier(6u, 0u, 0u, 0u) | (1u << 21u)},
    }};

    for (const auto& entry : rejectedCases) {
        const std::array<std::uint32_t, 2> words{ffbhI32Word, entry.modifier};
        try {
            static_cast<void>(DecodeRdnaInstruction(0u, words, 0u));
            std::fprintf(stderr, "%s decodes\n", entry.name);
            ++failures;
        } catch (const std::invalid_argument&) {
        } catch (const std::exception& error) {
            std::fprintf(stderr, "%s: unexpected error: %s\n", entry.name, error.what());
            ++failures;
        }
    }

    try {
        const std::array<std::uint32_t, 1> truncated{ffbhI32Word};
        static_cast<void>(RdnaInstructionDecoder{}.Decode(std::span(truncated)));
        std::fprintf(stderr, "truncated v_ffbh_i32 sdwa decodes\n");
        ++failures;
    } catch (const std::out_of_range&) {
    } catch (const std::exception& error) {
        std::fprintf(stderr, "truncated v_ffbh_i32 sdwa: unexpected error: %s\n", error.what());
        ++failures;
    }

    return failures == 0 ? 0 : 1;
}
