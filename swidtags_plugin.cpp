/*
 * Copyright (C) 2018--2024 Jan Pazdziora
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <libdnf5/base/base.hpp>

#include <libxml++/parsers/textreader.h>
#include <libxml++/document.h>
#include <ext/stdio_filebuf.h>

#include <filesystem>
#include <format>
#include <iostream>
#include <unordered_map>

#include <fcntl.h>
#include <errno.h>
#include <glob.h>

#include <rpm/rpmdb.h>
#include <rpm/rpmts.h>
#include <rpm/header.h>

using namespace libdnf5;

namespace {

constexpr const char * PLUGIN_NAME = "swidtags";
constexpr plugin::Version PLUGIN_VERSION{5, 0, 0};

constexpr const char * attrs[]{"author.name", "author.email", "description", nullptr};
constexpr const char * attrs_value[]{"Jan Pazdziora", "jan.pazdziora@code.adelton.com", "Plugin to keep SWID tags in sync with rpms."};

constexpr const char * LIBDNF_PLUGIN_SWIDTAGS_METADATA_TYPE = "swidtags";
constexpr const char * LIBDNF_PLUGIN_SWIDTAGS_DIR = "/etc/swid/swidtags.d";

#define debug(level, fmt, ...) \
	if (debug_level >= level) { std::cerr << std::format("[{}][{}] ", PLUGIN_NAME, level) << std::format(fmt __VA_OPT__(,) __VA_ARGS__) << std::endl; }

static bool xmlpp_next_element_at_depth(xmlpp::TextReader &reader, const int depth) {
	bool moved = false;
	while (reader.get_depth() < depth) {
		if (! reader.read()) {
			return false;
		}
		moved = true;
	}
	if (! moved && ! reader.next()) {
		return false;
	}
	while (reader.get_depth() == depth) {
		if (reader.get_node_type() == xmlpp::TextReader::Element) {
			return true;
		}
		if (! reader.next()) {
			return false;
		}
	}
	return false;
}

constexpr const char * SWIDTAGLIST_XMLNS = "http://rpm.org/metadata/swidtags.xsd";
constexpr const char * SWID_XMLNS = "http://standards.iso.org/iso/19770/-2/2015/schema.xsd";

static std::string escape_path(const std::string &in) {
	std::string result;
	for (auto it = in.begin(); it != in.end(); ++it) {
		char c = *it;
		if ((c >= 'a' && c <= 'z')
			|| (c >= 'A' && c <= 'Z')
			|| (c >= '0' && c <= '9')
			|| (c == '.' && it != in.begin())
			|| c == '_'
			|| c == ':'
			|| c == '-') {
			result += c;
		} else {
			char buffer[4];
			sprintf(buffer, "^%2x", c);
			result += std::string(buffer);
		}
	}
	return result;
}

#define _SWID_OUT_DIR_TEMPLATE "/usr/lib/swidtag/{}"
#define _SWID_OUT_FILE_TEMPLATE "{}-rpm-{}.swidtag"
#define _SWID_SWIDTAGS_D "/etc/swid/swidtags.d"
#define _SWID_DIR_SYMLINK_NAME_TEMPLATE _SWID_SWIDTAGS_D "/{}"
#define _SWID_DIR_SYMLINK_TARGET_TEMPLATE "../../../usr/lib/swidtag/{}"
#define _SWIDTAG_SUFFIX ".swidtag"
#define _SWIDTAG_COMPONENT_GLOB "-component-of-*.swidtag"

static void process_si_element(xmlpp::Node * si, const std::string &nevra, const std::string &checksum, const int debug_level) {

	auto tagId = std::string(si->eval_to_string("@tagId"));
	if (tagId.empty()) {
		debug(1, "{}: @tagId not found in SoftwareIdentity", nevra);
		return;
	}
	auto tagId_e = escape_path(tagId);
	if (tagId_e.empty()) {
		debug(1, "{}: failed to escape @tagId {}", nevra, tagId);
		return;
	}

	debug(7, "{}: @tagId {} (escaped)", nevra, tagId_e);

	auto regid = std::string(si->eval_to_string(
		"./swid:Entity[contains(concat(' ', @role, ' '), ' tagCreator ')]/@regid",
		{ std::pair("swid", SWID_XMLNS) }));
	auto regid_e = escape_path(regid);
	if (regid_e.empty()) {
		debug(1, "{}: failed to escape regid {}", nevra, regid);
		return;
	}
	debug(7, "{}: tagCreator @regid {} (escaped)", nevra, regid_e);

	auto dir = std::format(_SWID_OUT_DIR_TEMPLATE, regid_e);
	auto basename = std::format(_SWID_OUT_FILE_TEMPLATE, tagId_e, checksum);
	auto path = dir + "/" + basename;
	if (path.empty()) {
		debug(1, "{}: failed to create file path from {}, {}, and {}", nevra, regid_e, tagId_e, checksum);
		return;
	}
	debug(7, "{}: will store SWID tag into {}", nevra, path);

	if (mkdir(dir.c_str(), 0775) && errno != EEXIST) {
		debug(1, "failed to create output directory {}", dir);
		return;
	}
	int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
	if (fd == -1) {
		debug(1, "{}: failed to write SWID tag to {}: {}", nevra, path, strerror(errno));
		return;
	}
	debug(9, "{}: got fd {} for the output file", nevra, fd);
	__gnu_cxx::stdio_filebuf<char> fd_file_buf{fd, std::ios_base::out | std::ios_base::binary};
	std::ostream fd_stream{&fd_file_buf};

	xmlpp::Document doc;
	doc.create_root_node_by_import(si);
	doc.write_to_stream(fd_stream);
	debug(8, "{}: finished doc.write_to_stream", nevra);
	close(fd);
	debug(9, "{}: closed fd", nevra);
	debug(1, "written {} for {}", path, nevra);

	auto symlink_name = std::format(_SWID_DIR_SYMLINK_NAME_TEMPLATE, regid_e);
	if (symlink_name.empty()) {
		debug(1, "{}: failed allocating memory for directory symlink name", nevra);
		return;
	}

	struct stat st_buf;
	if (lstat(symlink_name.c_str(), &st_buf) == 0) {
		// We already have the symlink
		return;
	}

	auto symlink_target = std::format(_SWID_DIR_SYMLINK_TARGET_TEMPLATE, regid_e);
	if (symlink_target.empty()) {
		debug(1, "{}: failed allocating memory for directory symlink target", nevra);
		return;
	}
	std::filesystem::create_directory_symlink(symlink_target, symlink_name);
}

typedef std::string nevra_t;
typedef std::string checksum_t;
typedef std::string pkgid_t;
typedef std::unordered_map<pkgid_t, std::pair<nevra_t, checksum_t>> pkgids_hash_t;
typedef std::string repo_t;
typedef std::unordered_map<repo_t, pkgids_hash_t> repo_hashes_t;

static void add_swidtag_files_from_repo(const std::string &filename, pkgids_hash_t &pkgids, const int debug_level) {
	try {
		xmlpp::TextReader reader(filename);
		if (! xmlpp_next_element_at_depth(reader, 0)) {
			debug(1, "{}: failed to find root element", filename);
			return;
		}
		if (reader.get_namespace_uri() != SWIDTAGLIST_XMLNS || reader.get_local_name() != "swidtags") {
			debug(1, "{}: unexpected root element {{{}}}{}", filename,
				std::string(reader.get_namespace_uri()), std::string(reader.get_local_name()));
			return;
		}
		while (xmlpp_next_element_at_depth(reader, 1)) {
			if (reader.get_namespace_uri() != SWIDTAGLIST_XMLNS || reader.get_local_name() != "package") {
				continue;
			}
			auto pkgid = std::string(reader.get_attribute("pkgid"));
			if (pkgid == "") {
				debug(1, "{}: package element without @pkgid ignored", filename);
				continue;
			}
			auto p = pkgids.find(pkgid);
			if (p == pkgids.end()) {
				continue;
			}
			debug(6, "{} pkgid {} swidtags metadata entry found", p->second.first, pkgid);
			while (xmlpp_next_element_at_depth(reader, 2)) {
				if (reader.get_namespace_uri() == SWID_XMLNS && reader.get_local_name() == "SoftwareIdentity") {
					process_si_element(reader.expand(), p->second.first, p->second.second, debug_level);
				}
			}
			pkgids.erase(pkgid);
		}
		debug(6, "{}: ok", filename);
	} catch(const std::exception& e) {
		debug(1, "{}: {}", filename, e.what());
	}
}

static void add_swidtag_files(repo_hashes_t &added_repo_packages, const int debug_level) {
	for (auto& repo : added_repo_packages) {
		add_swidtag_files_from_repo(repo.first, repo.second, debug_level);
		for (const auto& p : repo.second) {
			debug(3, "  leftover {} -> {} : {}", p.first, p.second.first, p.second.second);
		}
	}
}


static std::string dnf_package_get_checksum(rpmts &ts, const std::string &nevra, const std::string &op, int debug_level) {
	rpmdbMatchIterator mi = rpmtsInitIterator(ts, RPMDBI_LABEL, nevra.c_str(), 0);
	if (mi == NULL) {
		debug(0, "{} {}: failed to init rpmdb iterator", op, nevra);
		return NULL;
	}
	Header h = rpmdbNextIterator(mi);
	if (h == NULL) {
		debug(0, "{} {}: failed to find package in rpmdb", op, nevra);
		rpmdbFreeIterator(mi);
		return NULL;
	}

	const char * sha = headerGetString(h, RPMTAG_SHA256HEADER);
	if (sha == NULL) {
		sha = headerGetString(h, RPMTAG_SHA1HEADER);
	}
	if (sha == NULL) {
		rpmdbFreeIterator(mi);
		debug(0, "{} {} has no SHA256HEADER", op, nevra);
		return NULL;
	}
	debug(3, "{} {} SHA256HEADER {}", op, nevra, sha);
	auto ret = std::string(sha);
	rpmdbFreeIterator(mi);
	return ret;
}

static void remove_swidtag_file(const std::string &checksum, const int debug_level) {
	std::string glob_path = std::format("{}/*/*-rpm-{}.swidtag", LIBDNF_PLUGIN_SWIDTAGS_DIR, checksum);
	debug(7, "globbing {}", glob_path);
	bool found = false;
	glob_t globbuf;
	if (glob(glob_path.c_str(), GLOB_NOSORT | GLOB_NOESCAPE, NULL, &globbuf) == 0) {
		char ** filename_co = globbuf.gl_pathv;
		while (*filename_co) {
			debug(1, "unlinking {}", *filename_co);
			if (unlink(*filename_co) == -1) {
				debug(0, "ERROR: unlink {} failed: {}", *filename_co, strerror(errno));
			}
			filename_co++;
			found = true;
		}
	}
	if (! found) {
		debug(8, "glob {} did not find anything to unlink", glob_path);
	}
	globfree(&globbuf);
}

class Swidtags : public plugin::IPlugin {
public:
	Swidtags(libdnf5::Base & base, libdnf5::ConfigParser &) : IPlugin(base) {}
	virtual ~Swidtags() {
		removed_packages_checksums.clear();
	}

	PluginAPIVersion get_api_version() const noexcept override { return PLUGIN_API_VERSION; }

	const char * get_name() const noexcept override { return PLUGIN_NAME; }

	plugin::Version get_version() const noexcept override { return PLUGIN_VERSION; }

	const char * const * get_attributes() const noexcept override { return attrs; }

	const char * get_attribute(const char * attribute) const noexcept override {
		for (size_t i = 0; attrs[i]; ++i) {
			if (std::strcmp(attribute, attrs[i]) == 0) {
				return attrs_value[i];
			}
		}
		return nullptr;
	}

	void init() override {
		char * debug_char = getenv("LIBDNF_PLUGIN_SWIDTAGS_DEBUG");
		if (debug_char) {
			debug_level = atoi(debug_char);
		}

		const auto & version = this->get_version();
		const auto & api_version = this->get_api_version();
		debug(5, "plugin version {}.{}.{} API version {}.{} in function {}",
			version.major, version.minor, version.micro,
			api_version.major, api_version.minor, __func__);
	}

	void pre_base_setup() override {
		debug(3, "hook pre_base_setup");
	}

	void post_base_setup() override {
		debug(3, "hook post_base_setup");
		libdnf5::Base & base = this->get_base();

		auto & config = base.get_config();
		config.get_optional_metadata_types_option().add_item(
			libdnf5::Option::Priority::RUNTIME, LIBDNF_PLUGIN_SWIDTAGS_METADATA_TYPE);
		debug(7, "  requesting {} metadata by adding it to the list of optional types", LIBDNF_PLUGIN_SWIDTAGS_METADATA_TYPE);
	}

	void pre_transaction(const libdnf5::base::Transaction & transaction) override {
		debug(3, "hook pre_transaction");
		libdnf5::Base & base = this->get_base();
		if (! base.is_initialized()) {
			debug(6, "base is not initialized in pre_transaction, skipping");
			return;
		}

		const auto & trans_packages = transaction.get_transaction_packages();

		rpmts ts = rpmtsCreate();
		rpmtsSetVSFlags(ts, _RPMVSF_NOSIGNATURES | _RPMVSF_NODIGESTS);
		for (auto & tpkg : trans_packages) {
			if (transaction_item_action_is_outbound(tpkg.get_action())) {
				auto cs = dnf_package_get_checksum(ts, tpkg.get_package().get_nevra().c_str(),
					transaction_item_action_to_string(tpkg.get_action()).c_str(), debug_level);
				if (! cs.empty()) {
					removed_packages_checksums[tpkg.get_package().get_nevra()] = cs;
				}
			}
		}
		rpmtsFree(ts);
	}

	void post_transaction(const libdnf5::base::Transaction & transaction) override {
		debug(3, "hook post_transaction");
		libdnf5::Base & base = this->get_base();

		libdnf5::repo::RepoQuery repos(base);
		repos.filter_enabled(true);

		const auto & trans_packages = transaction.get_transaction_packages();

		rpmts ts = rpmtsCreate();
		rpmtsSetVSFlags(ts, _RPMVSF_NOSIGNATURES | _RPMVSF_NODIGESTS);
		for (auto & tpkg : trans_packages) {
			if (transaction_item_action_is_outbound(tpkg.get_action())) {
				const auto & el = removed_packages_checksums.find(tpkg.get_package().get_nevra());
				if (el != removed_packages_checksums.end()) {
					debug(3, "Remove {} {}", tpkg.get_package().get_nevra(), el->second);
					remove_swidtag_file(el->second, debug_level);
				} else {
					debug(1, "Remove s {} has no SHA256HEADER noted", tpkg.get_package().get_nevra());
					continue;
				}
			}
		}

		repo_hashes_t added_repo_packages = {};
		for (auto & tpkg : trans_packages) {
			if (transaction_item_action_is_inbound(tpkg.get_action())) {
				auto nevra = tpkg.get_package().get_nevra();
				auto cs = dnf_package_get_checksum(ts, nevra.c_str(), transaction_item_action_to_string(tpkg.get_action()).c_str(), debug_level);
				if (cs.empty()) {
					continue;
				}
				auto repo = tpkg.get_package().get_repo();
				auto pkgid = tpkg.get_package().get_checksum().get_checksum();
				if (pkgid.empty()) {
					debug(4, "  no pkgid from repo {}", repo->get_id());
					continue;
				}
				auto metadata_path = repo->get_metadata_path(LIBDNF_PLUGIN_SWIDTAGS_METADATA_TYPE);
				if (metadata_path.empty()) {
					debug(5, "  pkgid {} from repo {} no {} metadata", pkgid, repo->get_id(), LIBDNF_PLUGIN_SWIDTAGS_METADATA_TYPE);
					continue;
				}
				debug(5, "  pkgid {} from {}", pkgid, repo->get_id());
				debug(3, "{} metadata for repo {}: {}", LIBDNF_PLUGIN_SWIDTAGS_METADATA_TYPE, repo->get_id(), metadata_path);
				added_repo_packages[metadata_path][pkgid] = std::make_pair(nevra, cs);
			}
		}
		rpmtsFree(ts);

		add_swidtag_files(added_repo_packages, debug_level);
	}

private:
	int debug_level = 0;
	std::unordered_map<std::string, std::string> removed_packages_checksums = {};
};

}  // namespace

PluginAPIVersion libdnf_plugin_get_api_version(void) {
	return PLUGIN_API_VERSION;
}

const char * libdnf_plugin_get_name(void) {
	return PLUGIN_NAME;
}

plugin::Version libdnf_plugin_get_version(void) {
	return PLUGIN_VERSION;
}

plugin::IPlugin * libdnf_plugin_new_instance(
	[[maybe_unused]] LibraryVersion library_version,
	libdnf5::Base & base,
	libdnf5::ConfigParser & parser) try {
	return new Swidtags(base, parser);
} catch (...) {
	return nullptr;
}

void libdnf_plugin_delete_instance(plugin::IPlugin * plugin_object) {
	delete plugin_object;
}

