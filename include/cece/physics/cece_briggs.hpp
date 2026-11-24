#ifndef CECE_BRIGGS_HPP
#define CECE_BRIGGS_HPP

#include "cece/physics_scheme.hpp"

namespace cece {

class BriggsScheme : public BasePhysicsScheme {
   public:
    void Run(CeceImportState& import_state, CeceExportState& export_state) override;
};

}  // namespace cece

#endif
