#pragma once

#include "duckdb/catalog/catalog_entry/table_catalog_entry.hpp"
#include "duckdb/parser/column_list.hpp"
#include "duckdb/parser/parsed_data/create_table_info.hpp"

#include "core/glue_info.hpp"

namespace duckdb {
class GlueCatalog;

//! A table registered in the Glue Data Catalog. Only Hive (Glue native) tables can be scanned and written; tables
//! of other formats (Iceberg, Delta, ...) are listed with the columns Glue reports but can not be read.
class GlueTable : public TableCatalogEntry {
public:
	GlueTable(Catalog &catalog, SchemaCatalogEntry &schema, CreateTableInfo &info, GlueTableInfo table_info);

public:
	const ColumnList &GetColumns() const override;
	unique_ptr<BaseStatistics> GetStatistics(ClientContext &context, column_t column_id) override;
	TableFunction GetScanFunction(ClientContext &context, unique_ptr<FunctionData> &bind_data) override;
	TableStorageInfo GetStorageInfo(ClientContext &context) override;
	//! filename: the location of the data file a row was read from, as for read_parquet and hive_scan
	virtual_column_map_t GetVirtualColumns() const override;
	//! None: the files of a Hive table have no row identifier
	vector<column_t> GetRowIdColumns() const override;

	//! Re-fetch the table definition from Glue
	GlueTableInfo RefreshTableInfo(ClientContext &context) const;
	//! "a Hive table ("t" in Glue database "db")", for the errors of statements Glue tables do not support
	string DescribeForError() const;
	optional_ptr<CatalogEntry> CreateTrigger(CatalogTransaction transaction, CreateTriggerInfo &info) override;
	//! Refuses the UPDATE while it is bound: a Hive table has no row ids to update by
	void BindUpdateConstraints(Binder &binder, LogicalGet &get, LogicalProjection &proj, LogicalUpdate &update,
	                           ClientContext &context) override;

private:
	//! Scan a Hive table with read_parquet over the files of the partitions Glue lists (or the table location for an
	//! unpartitioned table), using the HiveMultiFileReader for Glue's schema and partition values
	TableFunction GetHiveScanFunction(ClientContext &context, unique_ptr<FunctionData> &bind_data,
	                                  const GlueTableInfo &latest_info);

public:
	//! The table definition as returned by Glue when the entry was created
	GlueTableInfo table_info;

private:
	//! The columns of the table: data columns first, partition keys last
	ColumnList columns;
};

} // namespace duckdb
