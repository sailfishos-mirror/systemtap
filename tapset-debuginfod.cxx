// tapset for debuginfod searching
// Copyright (C) 2005-2023 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include "session.h"
#include "tapsets.h"
#include "translate.h"
#include "util.h"

#if defined(HAVE_LIBDEBUGINFOD) && defined(HAVE_JSON_C)

#include <elfutils/debuginfod.h>
#include <json-c/json.h>
#include <unistd.h>

using namespace std;
using namespace __gnu_cxx;

static const string TOK_DEBUGINFOD("debuginfod");
static const string TOK_PROCESS("process");

void
get_buildids(string process_path, set<interned_string>& buildids){
  static unique_ptr <debuginfod_client, void (*)(debuginfod_client*)>
    client (debuginfod_begin(), &debuginfod_end);

  int metadata_fd;
  #ifdef METADATA_QUERY_ENABLED
    if((metadata_fd = debuginfod_find_metadata(client.get(), "glob", (char*)process_path.c_str(), NULL)) < 0)
      throw SEMANTIC_ERROR(_("can't retrieve buildids from debuginfod"));
  #else
    throw SEMANTIC_ERROR(_F("can't retrieve buildids for %s from debuginfod. Metadata query is not supported", process_path.c_str()));
  #endif

  #ifdef HAVE_JSON_C
  vector<pid_t> debuginfod_workers;
  json_object *metadata = json_object_from_fd(metadata_fd);
  json_object *metadata_array;
  if(!metadata || !json_object_object_get_ex(metadata, "results", &metadata_array))
    throw SEMANTIC_ERROR(_("retrieved invalid buildids from debuginfod"));
  close(metadata_fd);
  size_t n_bid = json_object_array_length(metadata_array);

  if(0 == n_bid)
    throw SEMANTIC_ERROR(_("can't retrieve buildids from debuginfod"));

  int d_out, d_err;
  json_object *file_metadata, *json_field;
  string buildid;
  for(size_t i = 0; i < n_bid; i++)
  {
    file_metadata = json_object_array_get_idx(metadata_array, i);
    if(json_object_object_get_ex(file_metadata, "buildid", &json_field)) buildid = json_object_get_string(json_field);

    // Query debuginfod for executable/debuginfo for executables which have yet to be seen (as recorded by buildid)
    // Then the files will be cached for when they are needed later, when accessed via buildid
    interned_string buildid_is = interned_string(buildid);
    if(is_build_id(buildid) && buildids.find(buildid_is) == buildids.end() &&
        json_object_object_get_ex(file_metadata, "type", &json_field) && 0 == strcmp(json_object_get_string(json_field), "executable"))
    {
      buildids.insert(buildid_is);

      debuginfod_workers.push_back(stap_spawn_piped(0, {"debuginfod-find", "executable", buildid}, NULL, &d_out, &d_err));
      debuginfod_workers.push_back(stap_spawn_piped(0, {"debuginfod-find", "debuginfo" , buildid}, NULL, &d_out, &d_err));
    }
  }
  json_object_put(metadata);

  // Make sure all the executables/debuginfo are found before continuing
  for(auto worker_pid = debuginfod_workers.begin(); worker_pid != debuginfod_workers.end(); ++worker_pid)
    if(*worker_pid > 0) stap_waitpid(0, *worker_pid);
  #else
    throw SEMANTIC_ERROR(_F("can't retrieve buildids for %s from debuginfod. json-c not installed", process_path.c_str()));
  #endif
}

// ------------------------------------------------------------------------
// debuginfod_builder derived probes
// ------------------------------------------------------------------------
struct debuginfod_builder: public derived_probe_builder
{
public:
  void build(systemtap_session & sess, probe * base,
    probe_point * location,
    literal_map_t const & parameters,
    vector<derived_probe *> & finished_results);

  void build_with_suffix(systemtap_session & sess, probe * base,
    probe_point * location,
    literal_map_t const & parameters,
    std::vector<derived_probe *> & finished_results,
    std::vector<probe_point::component *> const & suffix);

  virtual string name() { return "debuginfod builder"; }
};

void
debuginfod_builder::build(systemtap_session & sess, probe * base,
  probe_point * location,
  literal_map_t const & parameters,
  vector<derived_probe *> & finished_results)
{
  interned_string process_path;
  bool has_debuginfod = has_null_param (parameters, TOK_DEBUGINFOD);
  bool has_process    = get_param (parameters, TOK_PROCESS, process_path);

  if(!has_debuginfod || !has_process)
    throw SEMANTIC_ERROR(_("the probe must be of the form debuginfod.process(\"foo/bar\").**{...}"));

  // The matching buildids from the packages/debuginfod
  set<interned_string> buildids;
  get_buildids(process_path, buildids);

  probe *base_p = new probe(base, location);
  probe_point *base_pp = base_p->locations[0];
  base_p->locations.clear();  // The new probe points are created below as derivatives of base_pp
  base_pp->components.erase(base_pp->components.begin()); // Remove the 'debuginfod'

  for(auto it = buildids.begin(); it != buildids.end(); ++it){
    interned_string buildid = *it;

    // Create a new probe point location.
    probe_point *pp = new probe_point(*base_pp);

    // The new probe point location might not have all wildcards
    // expanded, so the new location isn't well-formed.
    pp->well_formed = false;

    // Create a new 'process' component.
    probe_point::component* ppc
    = new probe_point::component (TOK_PROCESS,
          new literal_string(buildid),
          false);
    ppc->tok = base_pp->components[0]->tok;
    pp->components[0] = ppc;

    base_p->locations.push_back(pp);
  }

  vector<derived_probe *> results;
  derive_probes(sess, base_p, results, false, true);

  if(results.size() < buildids.size())
    throw SEMANTIC_ERROR (_F("at least %d probes should be derived but only %d were",
      (int)buildids.size(), (int)results.size()));

  finished_results.insert(finished_results.end(), results.begin(), results.end());
}

void
debuginfod_builder::build_with_suffix(systemtap_session & sess, probe * base,
    probe_point * location,
    literal_map_t const &,
    std::vector<derived_probe *> & finished_results,
    std::vector<probe_point::component *> const & suffix){

    /* Extract the parameters for the head of the probe
     * The probes are of the form debuginfod.process("foo/bar").**
     * So the length should always be TWO (2)
     */
    unsigned num_param = location->components.size() - suffix.size();
    assert(num_param == 2);

    literal_map_t param_map;
    for (unsigned i=0; i<num_param; i++)
      param_map[location->components[i]->functor] = location->components[i]->arg;

    build(sess, base, location, param_map, finished_results);
    return;
}


void
register_tapset_debuginfod(systemtap_session& s)
{
    match_node* root = s.pattern_root;
    derived_probe_builder *builder = new debuginfod_builder();

    // All suffixes will get caught and processed by build_with_suffix
    root->bind(TOK_DEBUGINFOD)->bind_str(TOK_PROCESS)->bind(builder);
}

#else /* no DEBUGINFOD and/or no JSON */

void
register_tapset_debuginfod(systemtap_session& s)
{
  (void) s;
}

#endif

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
