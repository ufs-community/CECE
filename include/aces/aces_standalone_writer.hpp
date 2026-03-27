#ifndef ACES_STANDALONE_WRITER_HPP
#define ACES_STANDALONE_WRITER_HPP

#include <Kokkos_Core.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "aces_compute.hpp"
#include "aces_config.hpp"

namespace aces {

class AcesStandaloneWriter {
   public:
    explicit AcesStandaloneWriter(const AcesOutputConfig& config);
    ~AcesStandaloneWriter();

    int Initialize(const std::string& start_time_iso8601, int nx, int ny, int nz);

    int InitializeWithCoords(const std::string& start_time_iso8601, int nx, int ny, int nz,
                           const std::vector<double>& lon_coords, const std::vector<double>& lat_coords);

    int WriteTimeStep(const std::unordered_map<std::string, DualView3D>& export_fields,
                      double time_seconds_since_start, int step_index);

    void Finalize();

    bool IsInitialized() const {
        return initialized_;
    }

   private:
    AcesOutputConfig config_;
    bool initialized_ = false;
    int record_count_ = 0;
    int nx_ = 0, ny_ = 0, nz_ = 0;
    std::string start_time_iso8601_;
    std::vector<double> lon_coords_;
    std::vector<double> lat_coords_;
    bool use_custom_coords_ = false;

    std::string ResolveFilename(double time_seconds_since_start) const;
};

}  // namespace aces

#endif
