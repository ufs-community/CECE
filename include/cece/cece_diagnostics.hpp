#ifndef CECE_DIAGNOSTICS_HPP
#define CECE_DIAGNOSTICS_HPP

#include <string>
#include <unordered_map>
#include <vector>

#include "cece/cece_compute.hpp"
#include "cece/cece_config.hpp"

namespace cece {

/**
 * @struct DiagnosticDeclaration
 * @brief A diagnostic recorded before its field can be allocated.
 *
 * Schemes take this path during Phase 1 (no grid known yet, or no manager
 * bound). Only metadata is retained; the requested placeholder dimensions are
 * intentionally ignored once the manager grid is set (FR-008).
 */
struct DiagnosticDeclaration {
    std::string name;       ///< Unique diagnostic name.
    std::string units;      ///< CF-compliant units (may be empty).
    std::string long_name;  ///< Descriptive name (may be empty).
};

/**
 * @struct DiagnosticFieldInfo
 * @brief A registered diagnostic: a grid-sized field plus its metadata.
 *
 * The DualView3D is shared process-wide for a given name (FR-003): every
 * scheme that registers the same name receives a handle aliasing this storage.
 */
struct DiagnosticFieldInfo {
    DualView3D data;        ///< Grid-sized field (extents == manager grid).
    std::string units;      ///< CF-compliant units.
    std::string long_name;  ///< Descriptive name.
};

/**
 * @class CeceDiagnosticManager
 * @brief Registry of diagnostic fields, sized to the realized grid.
 *
 * Lifecycle (data-model E3): diagnostics declared before the grid is known are
 * held as pending declarations; SetGrid promotes them (allocating at the real
 * grid extent) and makes all later registrations allocate immediately. The
 * registry is framework-free — no AMIO/ESMF assumptions — so its semantics are
 * unit-testable without a coupled framework (research R8 tier-1).
 */
class CeceDiagnosticManager {
   public:
    CeceDiagnosticManager() = default;

    /**
     * @brief Sets the authoritative grid dimensions and promotes pending fields.
     *
     * After this call every diagnostic allocates at (nx, ny, nz) regardless of
     * the placeholder dims requested at declaration time (FR-008). May be
     * called only once with a given extent; a conflicting re-set throws.
     *
     * @param nx Grid dimension in x.
     * @param ny Grid dimension in y.
     * @param nz Grid dimension in z.
     */
    void SetGrid(int nx, int ny, int nz);

    /**
     * @brief Declares names that diagnostics must never use (species names).
     * @param names Reserved names (typically the configured species).
     */
    void SetReservedNames(std::vector<std::string> names);

    /**
     * @brief Registers a diagnostic, returning the shared grid-sized field.
     *
     * Duplicate names return the existing field (FR-003). Coordinate names and
     * reserved species names throw, naming the offender (FR-009). Metadata is
     * first-non-empty-wins: a later non-empty units/long_name fills an unset
     * value but never clobbers an established one (FR-004, FR-016).
     *
     * Before SetGrid the declaration is recorded as pending and an empty handle
     * is returned; the field is allocated when the grid is set.
     *
     * @return DualView3D the shared field (grid-sized once the grid is set), or
     *         an empty handle while the declaration is still pending.
     */
    DualView3D RegisterDiagnostic(const std::string& name, int nx, int ny, int nz, const std::string& units = "", const std::string& long_name = "");

    /// @brief Read-only view of allocated fields (name -> field + metadata).
    const std::unordered_map<std::string, DiagnosticFieldInfo>& Fields() const {
        return fields_;
    }

    /// @brief Read-only view of pending declarations (name -> metadata), i.e.
    /// those declared before SetGrid promoted them. The C-ABI enumeration
    /// reports metadata for pending names through this view.
    const std::unordered_map<std::string, DiagnosticDeclaration>& PendingDeclarations() const {
        return pending_;
    }

    /// @brief Names of allocated fields in stable first-seen order (cap/manifest).
    const std::vector<std::string>& Order() const {
        return order_;
    }

    /**
     * @brief Warns, once per name, about selected diagnostics never registered.
     *
     * A `diagnostics:variables` entry that no scheme ever registered is a
     * non-fatal configuration gap (FR-013): publication simply omits it. The
     * warning is emitted the first time each offender is seen so repeated
     * publish steps don't spam the log.
     */
    void WarnUnregisteredSelection(const std::vector<std::string>& requested) const;

    /// @brief True once SetGrid has established the grid extent.
    bool HasGrid() const {
        return grid_set_;
    }

    int nx() const {
        return nx_;
    }
    int ny() const {
        return ny_;
    }
    int nz() const {
        return nz_;
    }

   private:
    // Rejects coordinate/bounds/mesh names and reserved species names.
    void ValidateName(const std::string& name) const;
    // Allocates a grid-sized, zero-initialized field and records its name.
    DiagnosticFieldInfo& Allocate(const DiagnosticDeclaration& declaration);

    bool grid_set_ = false;
    int nx_ = 0;
    int ny_ = 0;
    int nz_ = 0;

    std::vector<std::string> reserved_names_;
    std::unordered_map<std::string, DiagnosticDeclaration> pending_;
    std::unordered_map<std::string, DiagnosticFieldInfo> fields_;
    std::vector<std::string> order_;                           // first-seen order over pending_ then fields_
    mutable std::vector<std::string> warned_selection_names_;  // FR-013 warn-once state
};

}  // namespace cece

/* C-ABI surface consumed by the NUOPC cap (feature 003, contracts/diagnostics-c-api.md).
 * All entry points take the opaque cece_core_* data handle (a cece::CeceInternalData*)
 * and report through int* rc (0 = success, -1 = error with a logged message),
 * matching the existing cece_core_* convention. Strings are written
 * NUL-terminated into caller-owned buffers and are never silently truncated:
 * a buffer too small is an error. Definitions live in
 * src/core/cece_diagnostic_api.cpp. */
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Count the diagnostics the registry will enumerate (pending + allocated,
 * deduped, stable first-seen order). Valid once the core data exists.
 */
void cece_core_diagnostic_count(void* data_ptr, int* count, int* rc);

/** @brief Write the diagnostic name at @p index (0-based) into @p name. */
void cece_core_diagnostic_name(void* data_ptr, int index, char* name, int max_len, int* rc);

/** @brief Write the diagnostic units at @p index into @p units (empty string if unset). */
void cece_core_diagnostic_units(void* data_ptr, int index, char* units, int max_len, int* rc);

/** @brief Write the diagnostic long name at @p index into @p long_name (empty string if unset). */
void cece_core_diagnostic_long_name(void* data_ptr, int index, char* long_name, int max_len, int* rc);

/**
 * @brief Register a caller-owned buffer as the persistent write-back target
 * for the named diagnostic (cap Realize). The name must already be a
 * registered or pending diagnostic and the extents must match the core grid;
 * re-registering a name overwrites the previous pointer. The core never
 * frees the buffer (ESMF owns the memory).
 */
void cece_core_set_diagnostic_field(void* data_ptr, const char* name, int name_len, double* field_data, int nx, int ny, int nz, int* rc);

/**
 * @brief Count the diagnostics the NUOPC cap should advertise, enumerated
 * from the config YAML at @p config_path (path_len bytes). Valid before any
 * simulation exists; a disabled or empty selection yields zero.
 */
void cece_nuopc_diagnostic_count(const char* config_path, int path_len, int* count, int* rc);

/**
 * @brief Write the index-th advertised diagnostic name (0-based, config
 * order) into @p name. Same selection rules as cece_nuopc_diagnostic_count.
 * @p name_len_out receives the length excluding the NUL terminator.
 */
void cece_nuopc_diagnostic_name(const char* config_path, int path_len, int index, char* name, int max_len, int* name_len_out, int* rc);

#ifdef __cplusplus
}
#endif

#endif  // CECE_DIAGNOSTICS_HPP
