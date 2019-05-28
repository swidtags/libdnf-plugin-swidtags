/*
 * Copyright (C) 2018--2019 Jan Pazdziora
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

#include <libdnf/plugin/plugin.h>
#include <libdnf/libdnf.h>
#include <libxml/xmlstring.h>
#include <libxml/xmlreader.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>
#include <libxml/xmlsave.h>

#include <stdio.h>
#include <stdlib.h>

#include <fcntl.h>
#include <libgen.h>
#include <errno.h>
#include <unistd.h>
#include <glob.h>

#include <rpm/rpmdb.h>
#include <rpm/header.h>

#define LIBDNF_PLUGIN_NAME "swidtags"
#define LIBDNF_PLUGIN_VERSION "0.8.2"
#define LIBDNF_PLUGIN_SWIDTAGS_METADATA_TYPE "swidtags"

#define LIBDNF_PLUGIN_SWIDTAGS_DIR "/etc/swid/swidtags.d"
#define LIBDNF_PLUGIN_SWIDTAGS_GLOB LIBDNF_PLUGIN_SWIDTAGS_DIR "/*/*-rpm-%s.swidtag"

static const PluginInfo info = {
	.name = LIBDNF_PLUGIN_NAME,
	.version = LIBDNF_PLUGIN_VERSION,
};

struct _PluginHandle {
	PluginMode mode;
	DnfContext * context;
	GHashTable * remove_set_checksum;
};

const PluginInfo * pluginGetInfo(void) {
	return &info;
}

static int debug_level = 0;
#define debug(level, format, ...) \
	if (debug_level >= level) { fprintf(stderr, "[" LIBDNF_PLUGIN_NAME "][%i] ", level); fprintf(stderr, format, ## __VA_ARGS__); }

PluginHandle * pluginInitHandle(int version, PluginMode mode, DnfPluginInitData * initData) {
	char * debug_char = getenv("LIBDNF_PLUGIN_SWIDTAGS_DEBUG");
	if (debug_char) {
		debug_level = atoi(debug_char);
	}

	debug(5, "plugin version %s API version %i mode %i in function %s\n",
		info.version, version, mode, __func__);

	PluginHandle * handle = malloc(sizeof(*handle));
	if (handle == NULL) return NULL;

	handle->mode = mode;
	handle->context = pluginGetContext(initData);
	handle->remove_set_checksum = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	return handle;
}

void pluginFreeHandle(PluginHandle * handle) {
	if (handle == NULL) return;
	if (handle->remove_set_checksum) {
		g_hash_table_destroy(handle->remove_set_checksum);
	}
	free(handle);
}

static int xmlTextReaderNextElementAtDepth(xmlTextReaderPtr reader, int depth) {
	int ret;
	int start_depth = xmlTextReaderDepth(reader);
	if (start_depth == depth) {
		ret = xmlTextReaderNext(reader);
	} else {
		if (start_depth < depth) {
			start_depth++;
		}
		ret = xmlTextReaderRead(reader);
	}
	while (ret == 1) {
		if (xmlTextReaderDepth(reader) < start_depth) {
			return 2;
		}
		if (xmlTextReaderDepth(reader) == depth) {
			if (xmlTextReaderNodeType(reader) == XML_ELEMENT_NODE) {
				return 1;
			}
			ret = xmlTextReaderNext(reader);
		} else {
			ret = xmlTextReaderRead(reader);
		}
	}
	return ret;
}

static const xmlChar * SWIDTAGLIST_XMLNS = (const xmlChar *)"http://rpm.org/metadata/swidtags.xsd";
static const xmlChar * SWID_XMLNS = (const xmlChar *)"http://standards.iso.org/iso/19770/-2/2015/schema.xsd";

static xmlChar * escape_path(const xmlChar * in) {
	if (in == NULL) return NULL;
	size_t len = xmlStrlen(in);
	xmlChar * out = xmlMemMalloc(len * 3 + 1);
	if (out == NULL) return NULL;
	int dotstart = 1;
	xmlChar * p = out;
	while (*in) {
		if (*in == '.') {
			if (dotstart) {
				if (sprintf((char *)p, "^%2x", *in) < 3) {
					xmlMemFree(out);
					return NULL;
				}
				p += 3;
				goto next;
			}
		} else {
			dotstart = 0;
		}
		if ((*in >= 'a' && *in <= 'z')
			|| (*in >= 'A' && *in <= 'Z')
			|| (*in >= '0' && *in <= '9')
			|| *in == '.'
			|| *in == '_'
			|| *in == ':'
			|| *in == '-') {
			*p = *in;
			p++;
		} else {
			if (sprintf((char *)p, "^%2x", *in) < 3) {
				xmlMemFree(out);
				return NULL;
			}
			p += 3;
		}
	next:
		in++;
	}
	*p = '\0';
	return out;
}

#define _SWID_OUT_FILE_TEMPLATE "/usr/lib/swidtag/%s/%s-rpm-%s.swidtag"
#define _SWID_SWIDTAGS_D "/etc/swid/swidtags.d"
#define _SWID_DIR_SYMLINK_NAME_TEMPLATE _SWID_SWIDTAGS_D "/%s"
#define _SWID_DIR_SYMLINK_TARGET_TEMPLATE "../../../usr/lib/swidtag/%s"
#define _SWIDTAG_SUFFIX ".swidtag"
#define _SWIDTAG_COMPONENT_GLOB "-component-of-*.swidtag"

static void process_si_element(const xmlDocPtr orig_doc, const char * package, xmlNode * si) {
	xmlDocPtr doc = NULL;
	xmlXPathContextPtr xpath = NULL;
	xmlXPathObjectPtr xpath_obj = NULL;
	xmlBufferPtr regid_buf = NULL;
	xmlChar * tagId_e = NULL;
	xmlChar * regid_e = NULL;
	xmlChar * path = NULL;
	xmlChar * dir_x = NULL;
	xmlChar * symlink_name = NULL;
	xmlChar * symlink_target = NULL;

	xmlChar * tagId = xmlGetProp(si, (xmlChar *)"tagId");
	if (tagId == NULL) {
		debug(1, "%s: @tagId not found in SoftwareIdentity\n", package);
		goto next_child;
	}
	tagId_e = escape_path(tagId);
	xmlFree(tagId);
	if (tagId_e == NULL) {
		debug(1, "%s: failed to escape @tagId %s\n", package, tagId);
		goto next_child;
	}

	debug(7, "%s: @tagId %s\n", package, tagId_e);

	doc = xmlCopyDoc(orig_doc, 0);
	if (doc == NULL) {
		debug(1, "%s: failed to allocate internal xmlDoc for serialization\n", package);
		goto next_child;
	}
	xmlDocSetRootElement(doc, xmlCopyNode(si, 1));

	xpath = xmlXPathNewContext(doc);
	if (xpath == NULL) {
		debug(1, "%s: failed to allocate internal xmlXPathContext for serialization\n", package);
		goto next_child;
	}
	if (xmlXPathRegisterNs(xpath, (xmlChar *)"swid", SWID_XMLNS) != 0) {
		debug(1, "%s: failed register swid prefix to internal xmlXPathContext for serialization\n", package);
		goto next_child;
	}
	xpath_obj = xmlXPathEvalExpression((xmlChar *)"/swid:SoftwareIdentity/swid:Entity[contains(concat(' ', @role, ' '), ' tagCreator ')]/@regid", xpath);
	if (xpath_obj == NULL || xpath_obj->nodesetval == NULL
		|| xpath_obj->nodesetval->nodeNr < 0
		|| xpath_obj->nodesetval->nodeTab[0]->type != XML_ATTRIBUTE_NODE) {
		debug(1, "%s: failed to XPath-find tagCreator\n", package);
		goto next_child;
	}
	regid_buf = xmlBufferCreate();
	if (regid_buf == NULL) {
		debug(1, "%s: failed to allocate buffer for @regid\n", package);
		goto next_child;
	}
	xmlAttrSerializeTxtContent(regid_buf, doc,
		(xmlAttrPtr) xpath_obj->nodesetval->nodeTab[0], xpath_obj->nodesetval->nodeTab[0]->children->content);
	const xmlChar * regid = xmlBufferContent(regid_buf);
	if (regid == NULL) {
		debug(1, "%s: regid not found in SoftwareIdentity\n", package);
		goto next_child;
	}
	regid_e = escape_path(regid);
	if (regid_e == NULL) {
		debug(1, "%s: failed to escape regid %s\n", package, regid);
		goto next_child;
	}
	debug(7, "%s: tagCreator @regid %s\n", package, regid_e);

	const char * sha = package + strlen(package) + 1;
	int path_len = strlen(_SWID_OUT_FILE_TEMPLATE) - 6
		+ xmlStrlen(regid_e) + xmlStrlen(tagId_e) + strlen(sha) + 1;
	path = xmlMemMalloc(path_len);
	if (path == NULL) {
		debug(1, "%s: failed allocating memory for output file name\n", package);
		goto next_child;
	}
	if (snprintf((char *)path, path_len, _SWID_OUT_FILE_TEMPLATE, regid_e, tagId_e, sha) != path_len - 1) {
		debug(1, "%s: failed to construct output file name\n", package);
		goto next_child;
	}
	debug(7, "%s: will store SWID tag into %s\n", package, path);
	dir_x = xmlStrdup(path);
	if (dir_x == NULL) {
		debug(1, "%s: failed to construct output directory name\n", package);
		goto next_child;
	}
	char * dir = dirname((char *)dir_x);
	if (mkdir((char *)dir, 0775) && errno != EEXIST) {
		debug(1, "failed to create output directory %s\n", dir);
		goto next_child;
	}
	int fd = open((char *)path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
	if (fd == -1) {
		debug(1, "%s: failed to write SWID tag to %s: %s\n", package, path, strerror(errno));
		goto next_child;
	}
	debug(9, "%s: got fd %d for the output file\n", package, fd);
	xmlSaveCtxtPtr ctxt = xmlSaveToFd(fd, NULL, XML_SAVE_FORMAT);
	if (ctxt == NULL) {
		debug(1, "%s: failed to create serialization context\n", package);
		goto next_child;
	}
	debug(8, "%s: got xmlSaveToFd context\n", package);
	xmlSaveDoc(ctxt, doc);
	debug(8, "%s: finished xmlSaveDoc\n", package);
	xmlSaveClose(ctxt);
	debug(8, "%s: finished xmlSaveClose\n", package);
	close(fd);
	debug(9, "%s: closed fd\n", package);
	debug(1, "written %s for %s\n", path, package);

	int symlink_name_len = strlen(_SWID_DIR_SYMLINK_NAME_TEMPLATE) - 2 + xmlStrlen(regid_e) + 1;
	symlink_name = xmlMemMalloc(symlink_name_len);
	if (symlink_name == NULL) {
		debug(1, "%s: failed allocating memory for directory symlink name\n", package);
		goto next_child;
	}
	if (snprintf((char *)symlink_name, symlink_name_len, _SWID_DIR_SYMLINK_NAME_TEMPLATE, regid_e) != symlink_name_len - 1) {
		debug(1, "%s: failed to construct directory symlink name\n", package);
		goto next_child;
	}

	struct stat st_buf;
	if (lstat((char *)symlink_name, &st_buf) == 0) {
		// We already have the symlink
		goto next_child;
	}
	int symlink_target_len = strlen(_SWID_DIR_SYMLINK_TARGET_TEMPLATE) - 2 + xmlStrlen(regid_e) + 1;
	symlink_target = xmlMemMalloc(symlink_target_len);
	if (symlink_target == NULL) {
		debug(1, "%s: failed allocating memory for directory symlink target\n", package);
		goto next_child;
	}
	if (snprintf((char *)symlink_target, symlink_target_len, _SWID_DIR_SYMLINK_TARGET_TEMPLATE, regid_e) != symlink_target_len - 1) {
		debug(1, "%s: failed to construct directory symlink target\n", package);
		goto next_child;
	}
	if (symlink((char *)symlink_target, (char *)symlink_name) == -1) {
		debug(1, "%s: failed to create symlink %s pointing to %s: %s\n",
			package, symlink_name, symlink_target, strerror(errno));
		goto next_child;
	}

next_child:
	if (path) xmlMemFree(symlink_target);
	if (path) xmlMemFree(symlink_name);
	if (dir_x) xmlFree(dir_x);
	if (path) xmlMemFree(path);
	if (regid_e) xmlMemFree(regid_e);
	if (tagId_e) xmlMemFree(tagId_e);
	if (regid_buf) xmlBufferFree(regid_buf);
	if (xpath_obj) xmlXPathFreeObject(xpath_obj);
	if (xpath) xmlXPathFreeContext(xpath);
	if (doc) xmlFreeDoc(doc);
}

static void add_swidtag_files_from_repo(const gchar * filename, GHashTable * repo_pkgids) {
	xmlTextReaderPtr reader = xmlReaderForFile(filename, NULL, XML_PARSE_NOBLANKS | XML_PARSE_NONET);
	if (reader == NULL) {
		debug(1, "Unable to open %s\n", filename);
		return;
	}
	int ret = xmlTextReaderNextElementAtDepth(reader, 0);
	if (ret != 1) {
		debug(1, "%s: failed to find root element\n", filename);
		goto finish;
	}
	if (xmlStrcmp(xmlTextReaderConstLocalName(reader), (const xmlChar *)"swidtags")
		|| xmlStrcmp(xmlTextReaderConstNamespaceUri(reader), SWIDTAGLIST_XMLNS)) {
		debug(1, "%s: unexpected root element {%s}%s\n", filename,
			xmlTextReaderConstNamespaceUri(reader), xmlTextReaderConstLocalName(reader));
		goto finish;
	}

	xmlDocPtr orig_doc = xmlTextReaderCurrentDoc(reader);
	while ((ret = xmlTextReaderNextElementAtDepth(reader, 1)) == 1) {
		if (xmlStrcmp(xmlTextReaderConstLocalName(reader), (const xmlChar *)"package")
			|| xmlStrcmp(xmlTextReaderConstNamespaceUri(reader), SWIDTAGLIST_XMLNS)) {
			continue;
		}
		xmlChar * pkgid = xmlTextReaderGetAttribute(reader, (const xmlChar *)"pkgid");
		if (pkgid == NULL) {
			debug(1, "%s: package element without @pkgid ignored\n", filename);
			continue;
		}

		char * package = g_hash_table_lookup(repo_pkgids, pkgid);
		if (package == NULL) {
			xmlFree(pkgid);
			continue;
		}

		debug(6, "%s pkgid %s swidtags metadata entry found\n", package, pkgid);

		while ((ret = xmlTextReaderNextElementAtDepth(reader, 2)) == 1) {
			if (xmlStrcmp(xmlTextReaderConstLocalName(reader), (const xmlChar *)"SoftwareIdentity") == 0
				&& xmlStrcmp(xmlTextReaderConstNamespaceUri(reader), SWID_XMLNS) == 0) {
				process_si_element(orig_doc, package, xmlTextReaderExpand(reader));
			}
		}
		if (ret == 2) {
			// we did not stay at depth 2
			ret = 1;
		}

		g_hash_table_remove(repo_pkgids, pkgid);
		xmlFree(pkgid);
	}
	xmlFreeDoc(orig_doc);

	if (ret == 2) {
		debug(6, "%s: ok\n", filename);
	} else {
		debug(1, "%s: failed to parse\n", filename);
	}
finish:
	xmlFreeTextReader(reader);
}

static char * dnf_package_get_checksum(rpmts ts, const char * nevra, const char * op) {
	rpmdbMatchIterator mi = rpmtsInitIterator(ts, RPMDBI_LABEL, nevra, 0);
	if (mi == NULL) {
		debug(0, "%s %s: failed to init rpmdb iterator\n", op, nevra);
		return NULL;
	}
	Header h = rpmdbNextIterator(mi);
	if (h == NULL) {
		debug(0, "%s %s: failed to find package in rpmdb\n", op, nevra);
		rpmdbFreeIterator(mi);
		return NULL;
	}

	const char * sha = headerGetString(h, RPMTAG_SHA256HEADER);
	if (sha == NULL) {
		sha = headerGetString(h, RPMTAG_SHA1HEADER);
	}
	if (sha == NULL) {
		rpmdbFreeIterator(mi);
		debug(0, "%s %s has no SHA256HEADER\n", op, nevra);
		return NULL;
	}
	debug(3, "%s %s SHA256HEADER %s\n", op, nevra, sha);
	char * ret = g_strdup(sha);
	rpmdbFreeIterator(mi);
	return ret;
}

static void append_to_added_packages(rpmts ts, GHashTable * hash, GPtrArray * packages, const char * op) {
	if (packages == NULL) return;
	for (unsigned int i = 0; i < packages->len; ++i) {
		DnfPackage * pkg = g_ptr_array_index(packages, i);
		const char * nevra = dnf_package_get_nevra(pkg);
		char * checksum = dnf_package_get_checksum(ts, nevra, op);
		if (checksum == NULL) {
			debug(1, "  will not be able to store SWID tags even if some were available\n");
			continue;
		}

		const char * reponame = dnf_package_get_reponame(pkg);
		GHashTable * repo = g_hash_table_lookup(hash, reponame);
		if (repo == NULL) {
			repo = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
			g_hash_table_insert(hash, g_strdup(reponame), repo);
		}
		int type;
		const unsigned char * pkgid = dnf_package_get_chksum(pkg, &type);
		if (pkgid) {
			char * pkgid_str = hy_chksum_str(pkgid, type);
			debug(5, "  pkgid %s in repo %s\n", pkgid_str, reponame);
			g_hash_table_insert(repo, pkgid_str, g_strdup_printf("%s%c%s", nevra, 0, checksum));
		} else {
			debug(5, "  no pkgid in repo %s\n", reponame);
		}
		g_free(checksum);
	}
	g_ptr_array_unref(packages);
}

static void add_swidtag_files(DnfContext * context, HyGoal goal) {
	GHashTable * repos_packages = g_hash_table_new_full(g_str_hash, g_str_equal,
						g_free, (GDestroyNotify)g_hash_table_destroy);

	rpmts ts = rpmtsCreate();
	rpmtsSetVSFlags(ts, _RPMVSF_NOSIGNATURES | _RPMVSF_NODIGESTS);

	append_to_added_packages(ts, repos_packages, hy_goal_list_installs(goal, NULL), "installed");
	append_to_added_packages(ts, repos_packages, hy_goal_list_reinstalls(goal, NULL), "reinstalled");
	append_to_added_packages(ts, repos_packages, hy_goal_list_upgrades(goal, NULL), "upgraded");
	append_to_added_packages(ts, repos_packages, hy_goal_list_downgrades(goal, NULL), "downgraded");

	rpmtsFree(ts);

	if (g_hash_table_size(repos_packages) == 0) goto finish;
	unsigned int missed = 0;

	GPtrArray * repos = dnf_context_get_repos(context);
	for (unsigned int i = 0; i < repos->len; ++i) {
		DnfRepo * repo = g_ptr_array_index(repos, i);
		if (!( dnf_repo_get_enabled(repo) & DNF_REPO_ENABLED_PACKAGES )) continue;

		GHashTable * repo_pkgids = g_hash_table_lookup(repos_packages, dnf_repo_get_id(repo));
		if (repo_pkgids == NULL) continue;

		const gchar * swidtags = dnf_repo_get_filename_md(repo, LIBDNF_PLUGIN_SWIDTAGS_METADATA_TYPE);
		if (swidtags) {
			debug(3, "swidtags metadata for repo %s: %s\n", dnf_repo_get_id(repo), swidtags);
			add_swidtag_files_from_repo(swidtags, repo_pkgids);
		} else {
			debug(2, "no swidtags metadata for repo %s\n", dnf_repo_get_id(repo));
		}
		missed += g_hash_table_size(repo_pkgids);
	}
	if (missed > 0) {
		debug(1, "no SWID tags were found in metadata for %u packages\n", missed);
	}
finish:
	g_hash_table_destroy(repos_packages);
}

static void populate_remove_set_checksum_for(rpmts ts, GHashTable * hash,
	GPtrArray * packages, const char * op) {
	if (packages == NULL) return;
	for (unsigned int i = 0; i < packages->len; ++i) {
		DnfPackage * pkg = g_ptr_array_index(packages, i);
		const char * nevra = dnf_package_get_nevra(pkg);
		char * checksum = dnf_package_get_checksum(ts, nevra, op);
		if (checksum) {
			g_hash_table_insert(hash, g_strdup(nevra), checksum);
		}
	}
	g_ptr_array_unref(packages);
}

static void populate_remove_set_checksum(HyGoal goal, GHashTable * hash) {
	rpmts ts = rpmtsCreate();
	rpmtsSetVSFlags(ts, _RPMVSF_NOSIGNATURES | _RPMVSF_NODIGESTS);

	populate_remove_set_checksum_for(ts, hash, hy_goal_list_erasures(goal, NULL), "note removed");
	populate_remove_set_checksum_for(ts, hash, hy_goal_list_obsoleted(goal, NULL), "note obsoleted");
	populate_remove_set_checksum_for(ts, hash, hy_goal_list_reinstalls(goal, NULL), "note reinstalled");

	rpmtsFree(ts);
}

static void remove_swidtag_files_for(GHashTable * hash, GPtrArray * packages, const char * op) {
	if (packages == NULL) return;
	for (unsigned int i = 0; i < packages->len; ++i) {
		DnfPackage * pkg = g_ptr_array_index(packages, i);
		const char * nevra = dnf_package_get_nevra(pkg);
		const char * sha = g_hash_table_lookup(hash, nevra);
		if (sha == NULL) {
			debug(1, "%s %s has no SHA256HEADER noted\n", op, nevra);
			continue;
		}
		debug(2, "%s %s SHA256HEADER %s\n", op, nevra, sha);
		GString * glob_path = g_string_sized_new(strlen(LIBDNF_PLUGIN_SWIDTAGS_GLOB) + strlen(sha) + 1);
		g_string_printf(glob_path, LIBDNF_PLUGIN_SWIDTAGS_GLOB, sha);
		debug(7, "globbing %s\n", glob_path->str);
		glob_t globbuf;
		if (glob(glob_path->str, GLOB_NOSORT | GLOB_NOESCAPE, NULL, &globbuf) == 0) {
			char ** filename_co = globbuf.gl_pathv;
			while (*filename_co) {
				debug(1, "unlinking %s\n", *filename_co);
				if (unlink(*filename_co) == -1) {
					debug(0, "ERROR: unlink %s failed: %s\n", *filename_co, strerror(errno));
				}
			filename_co++;
			}
		}
		globfree(&globbuf);
		g_string_free(glob_path, TRUE);
	}
	g_ptr_array_unref(packages);
}

static void remove_swidtag_files(HyGoal goal, GHashTable * hash) {
	remove_swidtag_files_for(hash, hy_goal_list_erasures(goal, NULL), "removed");
	remove_swidtag_files_for(hash, hy_goal_list_obsoleted(goal, NULL), "obsoleted");
	remove_swidtag_files_for(hash, hy_goal_list_reinstalls(goal, NULL), "reinstalled");
}

int pluginHook(PluginHandle * handle, PluginHookId id, DnfPluginHookData * hookData, DnfPluginError * error) {
	if (handle == NULL) return 0;

	debug(5, "hook %i in function %s\n", id, __func__);
	switch (id) {
		case PLUGIN_HOOK_ID_CONTEXT_PRE_CONF:
			debug(3, "hook PLUGIN_HOOK_ID_CONTEXT_PRE_CONF\n");
			break;
		case PLUGIN_HOOK_ID_CONTEXT_PRE_REPOS_RELOAD:
			debug(3, "hook PLUGIN_HOOK_ID_CONTEXT_PRE_REPOS_RELOAD\n");
			break;
		case PLUGIN_HOOK_ID_CONTEXT_CONF:
			debug(3, "hook PLUGIN_HOOK_ID_CONTEXT_CONF\n");
			GPtrArray * repos = dnf_context_get_repos(handle->context);
			for (unsigned int i = 0; i < repos->len; ++i) {
				DnfRepo * repo = g_ptr_array_index(repos, i);
				if (dnf_repo_get_enabled(repo) & DNF_REPO_ENABLED_PACKAGES) {
					debug(7, "  requesting %s metadata for repo %s\n",
						LIBDNF_PLUGIN_SWIDTAGS_METADATA_TYPE,
						dnf_repo_get_id(repo));
					dnf_repo_add_metadata_type_to_download(repo,
						LIBDNF_PLUGIN_SWIDTAGS_METADATA_TYPE);
				}
			}
			break;
		case PLUGIN_HOOK_ID_CONTEXT_PRE_TRANSACTION:
			debug(3, "hook PLUGIN_HOOK_ID_CONTEXT_PRE_TRANSACTION\n");
			HyGoal goal = dnf_context_get_goal(handle->context);
			if (goal) {
				populate_remove_set_checksum(goal, handle->remove_set_checksum);
			}
			break;
		case PLUGIN_HOOK_ID_CONTEXT_TRANSACTION:
			debug(3, "hook PLUGIN_HOOK_ID_CONTEXT_TRANSACTION\n");
			goal = dnf_context_get_goal(handle->context);
			if (goal) {
				remove_swidtag_files(goal, handle->remove_set_checksum);
				add_swidtag_files(handle->context, goal);
			}
			break;
		default:
			debug(3, "ERROR: unknown hook id\n");
			break;
	}
	return 1;
}

