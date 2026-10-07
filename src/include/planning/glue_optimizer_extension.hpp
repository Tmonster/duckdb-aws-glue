#pragma once

#include "duckdb/main/config.hpp"

namespace duckdb {

//! Refuses VACUUM / ANALYZE of a Glue table before it runs: DuckDB has no catalog hook for it, and its own error only
//! comes once every file of the table has been scanned
void RegisterGlueOptimizerExtension(DBConfig &config);

} // namespace duckdb
