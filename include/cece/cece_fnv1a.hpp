// SPDX-License-Identifier: Apache-2.0
#ifndef CECE_FNV1A_HPP
#define CECE_FNV1A_HPP

#include <cstdint>

namespace cece::detail {

/// FNV-1a over 64-bit words, serialized least-significant byte first.
class Fnv1aHasher {
   public:
    void add(std::uint64_t value) {
        for (unsigned int shift = 0; shift < 64; shift += 8) {
            hash_ ^= (value >> shift) & 0xffULL;
            hash_ *= 1099511628211ULL;
        }
    }
    std::uint64_t value() const {
        return hash_;
    }

   private:
    std::uint64_t hash_{14695981039346656037ULL};
};

}  // namespace cece::detail
#endif
