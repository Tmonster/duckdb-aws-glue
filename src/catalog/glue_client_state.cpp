#include "catalog/glue_client_state.hpp"

#include "catalog/glue_table.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/parser/parsed_data/create_index_info.hpp"
#include "duckdb/parser/parsed_data/create_trigger_info.hpp"
#include "duckdb/parser/query_node/delete_query_node.hpp"
#include "duckdb/parser/query_node/merge_query_node.hpp"
#include "duckdb/parser/query_node/update_query_node.hpp"
#include "duckdb/parser/statement/create_statement.hpp"
#include "duckdb/parser/statement/delete_statement.hpp"
#include "duckdb/parser/statement/explain_statement.hpp"
#include "duckdb/parser/statement/merge_into_statement.hpp"
#include "duckdb/parser/statement/prepare_statement.hpp"
#include "duckdb/parser/statement/update_statement.hpp"
#include "duckdb/parser/statement/vacuum_statement.hpp"
#include "duckdb/parser/tableref/basetableref.hpp"

namespace duckdb {

GlueClientState &GlueClientState::Get(ClientContext &context) {
	return *context.registered_state->GetOrCreate<GlueClientState>(NAME);
}

void GlueClientState::QueryBegin(ClientContext &context) {
	lock_guard<mutex> guard(lock);
	bound_tables.clear();
}

void GlueClientState::QueryEnd(ClientContext &context) {
	lock_guard<mutex> guard(lock);
	bound_tables.clear();
}

void GlueClientState::AddBoundTable(const GlueTable &table) {
	lock_guard<mutex> guard(lock);
	bound_tables.push_back({table.catalog.GetName().GetIdentifierName(), table.schema.name.GetIdentifierName(),
	                        table.name.GetIdentifierName(), table.DescribeForError()});
}

optional_ptr<const GlueClientState::BoundTable> GlueClientState::FindTable(const QualifiedName &name) {
	lock_guard<mutex> guard(lock);
	for (auto &table : bound_tables) {
		if (!StringUtil::CIEquals(table.name, name.Name().GetIdentifierName())) {
			continue;
		}
		if (!name.Schema().empty() && !StringUtil::CIEquals(table.schema, name.Schema().GetIdentifierName())) {
			continue;
		}
		if (!name.Catalog().empty() && !StringUtil::CIEquals(table.catalog, name.Catalog().GetIdentifierName())) {
			continue;
		}
		return &table;
	}
	return nullptr;
}

static optional_ptr<const QualifiedName> BaseTableName(const unique_ptr<TableRef> &ref) {
	if (!ref || ref->type != TableReferenceType::BASE_TABLE) {
		return nullptr;
	}
	return &ref->Cast<BaseTableRef>().GetQualifiedName();
}

RebindQueryInfo GlueClientState::OnPlanningError(ClientContext &context, SQLStatement &statement, ErrorData &error) {
	if (error.Type() != ExceptionType::BINDER || !StringUtil::Contains(error.RawMessage(), "base table")) {
		return RebindQueryInfo::DO_NOT_REBIND;
	}
	if (statement.type == StatementType::PREPARE_STATEMENT) {
		return OnPlanningError(context, *statement.Cast<PrepareStatement>().statement, error);
	}
	if (statement.type == StatementType::EXPLAIN_STATEMENT) {
		return OnPlanningError(context, *statement.Cast<ExplainStatement>().stmt, error);
	}
	optional_ptr<const QualifiedName> target;
	QualifiedName index_table;
	string message;
	switch (statement.type) {
	case StatementType::UPDATE_STATEMENT:
		target = BaseTableName(statement.Cast<UpdateStatement>().node->table);
		message = "Cannot UPDATE %s";
		break;
	case StatementType::DELETE_STATEMENT:
		target = BaseTableName(statement.Cast<DeleteStatement>().node->table);
		message = "Cannot DELETE from %s";
		break;
	case StatementType::MERGE_INTO_STATEMENT:
		target = BaseTableName(statement.Cast<MergeIntoStatement>().node->target);
		message = "Cannot MERGE INTO %s";
		break;
	case StatementType::VACUUM_STATEMENT: {
		auto &info = *statement.Cast<VacuumStatement>().info;
		target = BaseTableName(info.ref);
		message = info.options.analyze && !info.options.vacuum ? "Cannot ANALYZE %s" : "Cannot VACUUM %s";
		break;
	}
	case StatementType::CREATE_STATEMENT: {
		auto &info = *statement.Cast<CreateStatement>().info;
		if (info.type == CatalogType::INDEX_ENTRY) {
			auto &index_info = info.Cast<CreateIndexInfo>();
			auto &index_name = index_info.GetQualifiedName();
			index_table = QualifiedName(index_name.Catalog(), index_name.Schema(), index_info.table);
			target = &index_table;
			message = "Cannot CREATE INDEX on %s";
		} else if (info.type == CatalogType::TRIGGER_ENTRY) {
			auto &trigger_info = info.Cast<CreateTriggerInfo>();
			if (trigger_info.base_table) {
				target = &trigger_info.base_table->GetQualifiedName();
			}
			message = "CREATE TRIGGER is not supported for tables in a Glue catalog, the trigger is on %s";
		}
		break;
	}
	default:
		break;
	}
	if (!target) {
		return RebindQueryInfo::DO_NOT_REBIND;
	}
	auto table = FindTable(*target);
	if (!table) {
		return RebindQueryInfo::DO_NOT_REBIND;
	}
	throw NotImplementedException(message, table->description);
}

} // namespace duckdb
