#include "planning/glue_optimizer_extension.hpp"

#include "catalog/glue_table.hpp"
#include "duckdb/optimizer/optimizer_extension.hpp"
#include "duckdb/planner/operator/logical_vacuum.hpp"

namespace duckdb {

static void RefuseGlueVacuum(LogicalOperator &op) {
	if (op.type == LogicalOperatorType::LOGICAL_VACUUM) {
		auto &vacuum = op.Cast<LogicalVacuum>();
		if (vacuum.table && vacuum.table->catalog.GetCatalogType() == "glue") {
			auto &options = vacuum.GetInfo().options;
			throw NotImplementedException(options.analyze && !options.vacuum ? "Cannot ANALYZE %s" : "Cannot VACUUM %s",
			                              vacuum.table->Cast<GlueTable>().DescribeForError());
		}
	}
	for (auto &child : op.children) {
		RefuseGlueVacuum(*child);
	}
}

static void GluePreOptimize(OptimizerExtensionInput &input, unique_ptr<LogicalOperator> &plan) {
	RefuseGlueVacuum(*plan);
}

void RegisterGlueOptimizerExtension(DBConfig &config) {
	OptimizerExtension extension;
	extension.pre_optimize_function = GluePreOptimize;
	OptimizerExtension::Register(config, std::move(extension));
}

} // namespace duckdb
