#ifndef CECE_IO_HPP
#define CECE_IO_HPP

#include <Kokkos_Core.hpp>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace cece {
namespace io {

class CeceIO {
   public:
    CeceIO() = default;
    ~CeceIO() = default;

    void Initialize(const std::filesystem::path& config_file, int nx, int ny, int nz);
    void Finalize();

    std::vector<std::string> GetOutputVarNames() const {
        return var_names_;
    }
    Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace> GetFieldView(const std::string& name);

   private:
    int nx_ = 0, ny_ = 0, nz_ = 1;
    using DeviceView = Kokkos::View<double***, Kokkos::LayoutLeft, Kokkos::DefaultExecutionSpace>;
    std::unordered_map<std::string, DeviceView> field_views_;
    std::vector<std::string> var_names_;
};

}  // namespace io
}  // namespace cece

#endif  // CECE_IO_HPP
