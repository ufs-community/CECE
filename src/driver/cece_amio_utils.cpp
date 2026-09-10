#include "cece/cece_amio_utils.hpp"

#include <amio/amio.h>

#include <cstdint>

namespace cece {
namespace detail {

std::size_t amio_dtype_size(amio_dtype_t dtype) {
    switch (dtype) {
        case AMIO_DTYPE_I8:
        case AMIO_DTYPE_U8:
            return 1;
        case AMIO_DTYPE_I16:
        case AMIO_DTYPE_U16:
            return 2;
        case AMIO_DTYPE_F32:
        case AMIO_DTYPE_I32:
        case AMIO_DTYPE_U32:
            return 4;
        case AMIO_DTYPE_F64:
        case AMIO_DTYPE_I64:
        case AMIO_DTYPE_U64:
            return 8;
        default:
            return 0;
    }
}

bool widen_amio_elements(const void* data, amio_dtype_t dtype, std::size_t n, double scale, double offset, std::vector<double>& out) {
    if (data == nullptr) return false;
    out.resize(n);

    auto convert = [&](auto tag) {
        using T = decltype(tag);
        const T* p = static_cast<const T*>(data);
        for (std::size_t k = 0; k < n; ++k) {
            out[k] = static_cast<double>(p[k]) * scale + offset;
        }
    };

    switch (dtype) {
        case AMIO_DTYPE_F32:
            convert(float{});
            return true;
        case AMIO_DTYPE_F64:
            convert(double{});
            return true;
        case AMIO_DTYPE_I8:
            convert(std::int8_t{});
            return true;
        case AMIO_DTYPE_I16:
            convert(std::int16_t{});
            return true;
        case AMIO_DTYPE_I32:
            convert(std::int32_t{});
            return true;
        case AMIO_DTYPE_I64:
            convert(std::int64_t{});
            return true;
        case AMIO_DTYPE_U8:
            convert(std::uint8_t{});
            return true;
        case AMIO_DTYPE_U16:
            convert(std::uint16_t{});
            return true;
        case AMIO_DTYPE_U32:
            convert(std::uint32_t{});
            return true;
        case AMIO_DTYPE_U64:
            convert(std::uint64_t{});
            return true;
        default:
            out.clear();
            return false;
    }
}

void read_cf_packing(amio_dataset_handle dataset, const std::string& var, double& scale, double& offset) {
    scale = 1.0;
    offset = 0.0;
    double value = 0.0;
    if (amio_get_var_attribute_double(dataset, var.c_str(), "scale_factor", &value) == AMIO_OK) scale = value;
    if (amio_get_var_attribute_double(dataset, var.c_str(), "add_offset", &value) == AMIO_OK) offset = value;
}

}  // namespace detail
}  // namespace cece
