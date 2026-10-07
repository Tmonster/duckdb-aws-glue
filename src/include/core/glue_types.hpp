#pragma once

#include "duckdb/common/string.hpp"
#include "duckdb/common/types.hpp"
#include "duckdb/common/types/value.hpp"
#include "core/glue_info.hpp"

namespace duckdb {
class ClientContext;

//! Conversion between Glue (Hive style) type strings and DuckDB logical types
struct GlueTypes {
	//! Parse a Glue column type string, e.g. 'int', 'decimal(10,2)', 'array<string>', 'struct<a:int,b:string>'
	static LogicalType ToLogicalType(const string &glue_type);
	//! Produce a Glue column type string from a DuckDB logical type
	static string FromLogicalType(const LogicalType &type);
	//! The column's DuckDB type: its Glue type, or the DuckDB type kept in the column's parameters (e.g. JSON, which
	//! is a Glue string)
	static LogicalType ToLogicalType(const GlueColumn &column);
	//! Set the column's Glue type, and keep a DuckDB type the Glue type does not carry in the column's parameters
	static void SetColumnType(GlueColumn &column, const LogicalType &type);

	//! HivePartitioning::GetValue without the unescaping: Glue stores the raw value, so 'a%20b' stays 'a%20b', not 'a
	//! b'. Throws if the value can't be cast to the key's type.
	static Value PartitionValue(ClientContext &context, const string &key, const string &str_value,
	                            const LogicalType &type);
};

} // namespace duckdb
