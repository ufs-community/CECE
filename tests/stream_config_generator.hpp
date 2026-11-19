#pragma once
#ifndef STREAM_CONFIG_GENERATOR_HPP
#define STREAM_CONFIG_GENERATOR_HPP

#include <rapidcheck.h>

namespace cece {
namespace {

// Generate an arbitrary StreamConfig. Every field is populated with arbitrary
// (but constrained-to-sane) values so the tests exercise the key derivation
// against noise in the NON-key fields (input_var_name, mapalgo, cadence,
// tintalgo, data_model_explicit) to prove they do NOT affect HandleKey.
// input_file_path and data_model are drawn from a broad string space; the
// integer knobs are bounded to sane ranges. The manifest-relevant fields
// (input_file_path, data_model, amio_worker_threads, amio_staging_buffer_count,
// amio_staging_buffer_capacity_bytes, amio_prefetch_depth) and mapalgo are
// drawn from a broad space to stress the concatenation formulas.
rc::Gen<StreamConfig> genStreamConfig() {
    return rc::gen::apply(
        [](std::string input_file_path, std::string input_var_name, std::string mapalgo, std::string cadence, std::string tintalgo,
           std::string data_model, bool data_model_explicit, int amio_worker_threads, int amio_staging_buffer_count,
           int amio_staging_buffer_capacity_bytes, int amio_prefetch_depth) {
            StreamConfig cfg;
            cfg.input_file_path = std::move(input_file_path);
            cfg.input_var_name = std::move(input_var_name);
            cfg.mapalgo = std::move(mapalgo);
            cfg.cadence = std::move(cadence);
            cfg.tintalgo = std::move(tintalgo);
            cfg.data_model = std::move(data_model);
            cfg.data_model_explicit = data_model_explicit;
            cfg.amio_worker_threads = amio_worker_threads;
            cfg.amio_staging_buffer_count = amio_staging_buffer_count;
            cfg.amio_staging_buffer_capacity_bytes = amio_staging_buffer_capacity_bytes;
            cfg.amio_prefetch_depth = amio_prefetch_depth;
            return cfg;
        },
        rc::gen::arbitrary<std::string>(), rc::gen::arbitrary<std::string>(), rc::gen::arbitrary<std::string>(), rc::gen::arbitrary<std::string>(),
        rc::gen::arbitrary<std::string>(), rc::gen::arbitrary<std::string>(), rc::gen::arbitrary<bool>(), rc::gen::inRange(1, 65),
        rc::gen::inRange(1, 65), rc::gen::inRange(1, 65), rc::gen::inRange(1, 65));
}

}  // namespace
}  // namespace cece

#endif
