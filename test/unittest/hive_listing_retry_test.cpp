#include "duckdb/common/local_file_system.hpp"
#include "duckdb/common/multi_file/multi_file_list.hpp"
#include "duckdb/common/mutex.hpp"
#include "duckdb/common/unordered_set.hpp"
#include "glue_test_util.hpp"

using namespace duckdb;

//! A listing that gives its first two files (ListLocation asks for them), then fails and keeps failing, as httpfs
//! keeps the error of a failed prefix
class FailingAfterTwoFilesList : public LazyMultiFileList {
public:
	explicit FailingAfterTwoFilesList(vector<OpenFileInfo> files_p)
	    : LazyMultiFileList(nullptr), files(std::move(files_p)) {
	}

protected:
	bool ExpandNextPath() const override {
		if (next >= files.size()) {
			return false;
		}
		if (next >= 2) {
			throw IOException("injected listing failure");
		}
		expanded_files.push_back(files[next++]);
		return true;
	}

private:
	vector<OpenFileInfo> files;
	mutable idx_t next = 0;
};

//! The local files below 'root'. The first listing of each pattern fails after its first two files, every listing of
//! one below '<root>/always' does
class FlakyListingFileSystem : public LocalFileSystem {
public:
	FlakyListingFileSystem(string root_p, shared_ptr<idx_t> listings_p)
	    : root(std::move(root_p)), listings(std::move(listings_p)) {
	}

	string GetName() const override {
		return "FlakyListingFileSystem";
	}
	bool CanHandleFile(const string &path) override {
		return StringUtil::StartsWith(path, root);
	}
	unique_ptr<MultiFileList> GlobFilesExtended(const string &path, const FileGlobInput &input,
	                                            optional_ptr<FileOpener> opener) override {
		bool fail;
		{
			lock_guard<mutex> guard(lock);
			(*listings)++;
			fail = failed.insert(path).second || StringUtil::StartsWith(path, root + "/always");
		}
		auto result = LocalFileSystem::GlobFilesExtended(path, input, opener);
		if (!fail) {
			return result;
		}
		return make_uniq<FailingAfterTwoFilesList>(result->GetAllFiles());
	}

private:
	string root;
	shared_ptr<idx_t> listings;
	mutex lock;
	unordered_set<string> failed;
};

static idx_t Count(Connection &con, const string &sql) {
	return Run(con, sql)->Fetch()->GetValue(0, 0).GetValue<idx_t>();
}

TEST_CASE("A directory listing that fails part way is listed again by the next reader", "[hive_scan]") {
	DuckDB db(nullptr);
	Connection con(db);
	auto &fs = FileSystem::GetFileSystem(*db.instance);
	auto root = fs.JoinPath(Env("TMPDIR", "/tmp"), "glue_listing_retry_" + Unique());
	auto listings = make_shared_ptr<idx_t>(0);
	fs.RegisterSubSystem(make_uniq<FlakyListingFileSystem>(root, listings));
	for (auto directory : {"", "/dense", "/dense/p=a", "/dense/p=b", "/flat", "/always"}) {
		fs.CreateDirectory(root + directory);
	}
	for (auto partition : {"a", "b"}) {
		for (idx_t file = 0; file < 3; file++) {
			auto path = root + "/dense/p=" + string(partition) + "/f" + std::to_string(file) + ".parquet";
			Run(con, "COPY (SELECT 1::BIGINT AS i) TO " + Quote(path));
			Run(con, "COPY (SELECT 1::BIGINT AS i) TO " +
			             Quote(root + "/flat/f" + std::to_string(file) + "_" + partition + ".parquet"));
		}
	}

	// the sample's listing of the root fails and is caught; the scan lists the root again instead of rethrowing
	Run(con, "SET hive_partition_listing_threshold = 1");
	auto dense = root + "/dense";
	auto partitions =
	    "[{p: 'a', location: " + Quote(dense + "/p=a") + "}, {p: 'b', location: " + Quote(dense + "/p=b") + "}]";
	*listings = 0;
	REQUIRE(Count(con, "SELECT count(*) FROM hive_scan(" + Quote(dense) +
	                       ", schema := {i: 'BIGINT', p: 'VARCHAR'}, partitions := " + partitions + ")") == 6);
	REQUIRE(*listings == 2);

	// a table without partitions, listed by the sample and the scan through the same directory listing
	*listings = 0;
	REQUIRE(Count(con, "SELECT count(*) FROM hive_scan(" + Quote(root + "/flat") + ", schema := {i: 'BIGINT'})") == 6);
	REQUIRE(*listings == 2);

	// a directory whose every listing fails still fails the query, with the listing's error
	auto always = root + "/always";
	for (idx_t file = 0; file < 3; file++) {
		Run(con, "COPY (SELECT 1::BIGINT AS i) TO " + Quote(always + "/f" + std::to_string(file) + ".parquet"));
	}
	auto result = con.Query("SELECT count(*) FROM hive_scan(" + Quote(always) + ", schema := {i: 'BIGINT'})");
	REQUIRE(result->HasError());
	REQUIRE(StringUtil::Contains(result->GetError(), "injected listing failure"));

	fs.RemoveDirectory(root);
}
