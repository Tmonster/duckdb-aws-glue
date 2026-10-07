#pragma once

#include "duckdb/common/mutex.hpp"
#include "duckdb/main/client_context_state.hpp"
#include "duckdb/parser/qualified_name.hpp"
#include "duckdb/planner/extension_callback.hpp"

namespace duckdb {
class GlueTable;

//! DuckDB's binder refuses UPDATE, DELETE, MERGE INTO, VACUUM, ANALYZE, CREATE INDEX and CREATE TRIGGER on any
//! table that is not a
//! DuckDB table with a generic "... base table" error before the Glue catalog is asked. This state remembers the Glue
//! tables a query binds and replaces that error with one naming the statement and the table.
class GlueClientState : public ClientContextState {
public:
	static constexpr const char *NAME = "glue_client_state";

	static GlueClientState &Get(ClientContext &context);

	//! OnPlanningError is only called when a state can request a rebind
	bool CanRequestRebind() override {
		return true;
	}
	void QueryBegin(ClientContext &context) override;
	void QueryEnd(ClientContext &context) override;
	RebindQueryInfo OnPlanningError(ClientContext &context, SQLStatement &statement, ErrorData &error) override;

	void AddBoundTable(const GlueTable &table);

private:
	struct BoundTable {
		string catalog;
		string schema;
		string name;
		string description;
	};

	optional_ptr<const BoundTable> FindTable(const QualifiedName &name);

private:
	mutex lock;
	vector<BoundTable> bound_tables;
};

//! Registers the GlueClientState on every new connection: DuckDB asks the states whether they handle planning errors
//! before binding, so a state created while binding comes too late for that statement
class GlueClientStateCallback : public ExtensionCallback {
public:
	void OnConnectionOpened(ClientContext &context) override {
		GlueClientState::Get(context);
	}
};

} // namespace duckdb
