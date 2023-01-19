// Language server code completion
// Copyright (C) 2022 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.
#include "config.h"
#include "staptree.h"
#include "parse.h"
#include "session.h"
#include "elaborate.h"
#include "stap-probe.h"
#include "stap-language-server.h"

#include <json-c/json.h>
#include <iostream>
#include <signal.h>
#include <sys/select.h>
#include <filesystem>
#include <numeric>

/* Utilities */

static string
pc2str(parse_context pc)
{
    switch (pc)
    {
    case con_unknown:  return "con_unknown";
    case con_probe:    return "con_probe";
    case con_function: return "con_function";
    case con_global:   return "con_global";
    case con_embedded: return "con_embedded";
    }
    return "undefined context";
}

static inline bool
in_body(string &code_snippet)
{
    return code_snippet.find('{') != string::npos;
}

/* Split a probe signature into a vector of component-param pairs
 * Ex. foo("a").bar(2).baz -> [ {"foo", "a"}, {bar, "2"}, {"baz", ""} ] 
 */
void tokenize_probe(const string &str, vector<pair<string, string>> &tokens_and_params)
{
    auto lex_probe_component = [&tokens_and_params](string full_token)
    {
        size_t p_start = full_token.find('(');
        if (p_start != string::npos)
        {
            tokens_and_params.push_back({
                full_token.substr(0, p_start),
                full_token.substr(p_start + 1, full_token.find(')', p_start) - p_start - 1)
            });
        }
        else
        {
            tokens_and_params.push_back({full_token, ""});
        }
    };

    // Walk the probe signature, until a new component is found
    // Lex the previous stringified component into a token & (possible) param
    // It is important to consider if we're in a string since '.' can appear within a string (Ex. libc.so)
    bool in_quote = false;
    size_t start = 0;
    for (size_t pos = 0; pos < str.size(); pos++)
    {
        char c = str[pos];
        if (c == '.' && !in_quote)
        {
            lex_probe_component(str.substr(start, pos - start));
            start = pos + 1;
        }
        else if (c == '\"')
        {
            in_quote = !in_quote;
        }
    }
    // Get that last component
    lex_probe_component(str.substr(start));
}

void lsp_method_text_document_completion::add_completion_item(string text, string type, string docs, string insert_text)
{
    lsp_object item;
    item.insert("label", text);
    if (!type.empty())
        item.insert("detail", type);
    if (!docs.empty())
        item.insert("documentation", docs);
    if (!insert_text.empty())
    {
        // Use the insertText if provided, this is the text that will be inserted as opposed to label
        item.insert("insertText", insert_text);
        // Using textEdit is preferable to insertText, but is not supported by all clients.
        // insertText is subject to interpretation by the client side
        // setting range's start and end to insert_position and using insert_text means append
        // the NEW part of the completion to the original (Ex. "foob<CURSOR>"
        // would have insert_text "ar" to get "foobar", with insert_position at <CURSOR>)
        lsp_object text_edit;
        text_edit.insert("range").insert("start", insert_position);
        text_edit.insert("range").insert("end", insert_position);
        text_edit.insert("newText", insert_text);
        item.insert("textEdit", text_edit);
    }
    completion_items.push_back(item);
}

void lsp_method_text_document_completion::complete_body(string &partial_statement)
{
    vector<string> body_components;
    tokenize(partial_statement, body_components, " \t\n{}");

    // The completion assumes body_components[:-1] are complete and we just want to
    // complete the final component
    string to_complete;
    if (body_components.size() != 0)
        to_complete = body_components.back();
    else
        to_complete = "";

    if (lang_server->verbose >= 2)
        cerr << "Completing [body]: '" << to_complete << "'" << endl;

    // FIXME: I miss things like print*
    if(startswith(to_complete, "$")){
        //TODO: Context vars
    }
    else if (!startswith(to_complete, "@"))
    {
        vector<map<string, functiondecl*>> functions = { doc->functions, lang_server->s->functions };
        for (auto f_list = functions.begin(); f_list != functions.end(); ++f_list)
            for (auto func = f_list->begin(); func != f_list->end(); ++func)
            {
                functiondecl *f = func->second;
                if (startswith(f->unmangled_name, to_complete))
                {
                    stringstream docs;
                    f->printsig(docs);
                    add_completion_item(f->unmangled_name.to_string(), "function", docs.str());
                }
            }
        vector<vector<vardecl*>> globals = { doc->globals, lang_server->s->globals };
        for (auto g_list = globals.begin(); g_list != globals.end(); ++g_list)
            for (auto global = g_list->begin(); global != g_list->end(); ++global)
            {
                vardecl *g = *global;
                if (startswith(g->unmangled_name, to_complete))
                {
                    stringstream docs;
                    g->printsig(docs);
                    add_completion_item(g->unmangled_name.to_string(), "global", docs.str());
                }
            }
        // FIXME: Perhaps I should pull the keywords and atwords from lexer in parse.cxx
        vector<string> keywords = {"probe", "global", "private", "function", "if", "else", "for", "foreach", "in",
                                   "limit", "return", "delete", "while", "break", "continue", "next", "string", "long", "try", "catch"};
        for (auto keyword = keywords.begin(); keyword != keywords.end(); ++keyword)
            if (startswith(*keyword, to_complete))
                add_completion_item(*keyword, "keyword");
    }
    else
    {
        string at_to_complete = to_complete.substr(1); // Remove the @
        for (auto macro = lang_server->s->library_macros.begin(); macro != lang_server->s->library_macros.end(); ++macro)
        {
            macrodecl *m = macro->second;
            string name = m->tok->content;
            if (startswith(name, at_to_complete))
            {
                stringstream docs;
                docs << name << "(" << join_vector(m->formal_args, ", ") << ")";
                // insertText is used here since LS clients have trouble matching @xyz so
                // when matching @x insert only yz
                add_completion_item(name, "macro", docs.str(), name.substr(at_to_complete.size()));
            }
        }
        vector<string> atwords = {"cast", "defined", "probewrite", "entry", "perf", "var", "avg", "count", "sum",
                                  "min", "max", "hist_linear", "hist_log", "const", "variance", "kregister", "uregister", "kderef", "uderef"};
        for (auto atword = atwords.begin(); atword != atwords.end(); ++atword)
            if (startswith(*atword, at_to_complete))
                add_completion_item(*atword, "atword");
    }
}

void lsp_method_text_document_completion::complete_probe_signature(string &partial_signature)
{
    vector<pair<string, string>> probe_components;
    tokenize_probe(partial_signature, probe_components);

    // The completion assumes probe_components[:-1] are complete and we just want to
    // complete the final component
    string to_complete = probe_components.back().first;
    if (lang_server->verbose >= 2)
        cerr << "Completing [probe signature]: '" << to_complete << "'" << endl;

    match_node *node = lang_server->s->pattern_root;
    // Find the current position within the match_node tree (which will be a parent of the result(s))
    for (auto ip = probe_components.begin(); ip != probe_components.end()-1; ++ip)
    {
        string comp = ip->first;
        string param = ip->second;

        match_key key = match_key(interned_string(comp));
        key.have_parameter = !param.empty();
        key.parameter_type = param.find('"') != param.npos ? pe_string : pe_long;

        for (match_node::sub_map_iterator_t it = node->sub.begin(); it != node->sub.end(); ++it)
            if (it->first.str() == key.str() && (!key.have_parameter || it->first.parameter_type == key.parameter_type))
            {
                node = it->second;
                break;
            }
    }

    // Find the matching node, which completes the component (to_complete)
    for (match_node::sub_map_iterator_t it = node->sub.begin(); it != node->sub.end(); ++it)
        if (startswith(it->first.str(), to_complete))
        {
            string docs = it->first.str();
            string comp = it->first.name.to_string();

            // LS clients have trouble distinguishing completion items with the same label
            // even if their signatures are different, so having foo, foo(" and foo( serves this purpose
            if (it->first.have_parameter)
                comp = comp + (it->first.parameter_type == pe_string ? "(\"" : "(");

            add_completion_item(comp, "probe component", docs);
        }
}

void lsp_method_text_document_completion::complete_path(string path)
{
    // TODO: PATH can have wildcards which means globbing needs to be supported
    if (path.empty())
        return;

    path = path.substr(1); // Remove the opening quote (since we're completing strings, there is no closing)

    vector<string> dirs;
    string partial_path = ""; // The path from the start (not including a slash at idx 0) to the last slash (inclusive)
    bool absolute;

    size_t last_slash = path.find_last_of('/');
    string to_complete = path.substr(last_slash + 1);

    if (path[0] == '/')
    {
        absolute = true;
        dirs.push_back("/");

        if (last_slash != string::npos && path.size() >= 1)
            partial_path = path.substr(1, last_slash);
    }
    else
    {
        absolute = false;

        const char *PATH = getenv("PATH");
        if (PATH)
            tokenize(string(PATH), dirs, string(":"));
        dirs.push_back(""); // Look at the current dir as well

        if (last_slash != string::npos)
            partial_path = path.substr(0, last_slash + 1);
    }

    for (auto dir : dirs)
        try
        {
            // If dir is a prefix of partial_path then just look at partial_path
            // otherwise assume that dir is in PATH and partial path is an unexplored tree within dir
            if (!partial_path.empty() && startswith(partial_path, dir.c_str()))
                dir = partial_path;
            else
                dir += (dir.back() != '/' ? "/" : "") + partial_path;

            for (const auto &file : std::filesystem::directory_iterator(dir))
            {
                string prefix = dir + (dir.back() != '/' ? "/" : "") + to_complete;
                if (startswith(file.path(), prefix) && (file.is_directory() || file.is_regular_file()))
                {
                    string type = file.is_directory() ? "directory" : "file";
                    if (absolute)
                        add_completion_item(file.path(), type, "", string(file.path()).substr(prefix.size()));
                    else
                        add_completion_item(file.path().filename(), type, "", string(file.path()).substr(prefix.size()));
                }
            }
        }
        catch (...)
        {} // If there is an issue just skip
}

void lsp_method_text_document_completion::complete_string(string &partial_signature)
{
    vector<pair<string, string>> probe_components;
    tokenize_probe(partial_signature, probe_components);

    // The completion assumes probe_components[:-1] are complete and we just want to
    // complete the final component
    string to_complete = probe_components.back().second; // The string param to complete
    string completion_component = probe_components.back().first;
    probe_components.pop_back();

    if (lang_server->verbose >= 2)
        cerr << "Completing [string]: '" << to_complete << "'" << endl;

    // TODO: There are still probe components to handle; see man stapprobes
    if (completion_component == "process" || completion_component == "procfs" || completion_component == "library") // The PATHlike components
    {
        return complete_path(to_complete);
    }
    else // Try our luck with wildcard expansion as the param, this logic is the same as that used by stap -L
    {
        try
        {
            // eat the last * token ex. "ma* should complete in the same way "ma would
            // we'll result in ma* completing as ma*n where as ma would complete as main
            bool has_wildcard_tail = '*' == to_complete.back();
            if (has_wildcard_tail) to_complete.pop_back();

            stringstream ss(string("probe ") + partial_signature + "*\") {}");
            probe *p = parse_synthetic_probe(*lang_server->s, ss, NULL);
            if (p)
            {
                vector<derived_probe *> dps;

                derive_probes(*lang_server->s, p, dps, false /* Not optional */, true /* Rethrow semantic errors */);
                for (auto it = dps.begin(); it != dps.end(); ++it)
                {
                    derived_probe *p = *it;
                    ostringstream o, docs;
                    o << *(*it)->sole_location()->components.back()->arg;

                    // TODO: Might want to grab p->locals[j] as well
                    list<string> args;
                    p->getargs(args);
                    if (args.size() > 0)
                    {
                        docs << "Target Vars: ";
                        for (auto ia = args.begin(); ia != args.end(); ++ia)
                            docs << *ia << " ";
                    }
                    // The insertion text is just the tail of the completion
                    string insert_text = o.str().substr(to_complete.size());
                    if(has_wildcard_tail) insert_text.erase(insert_text.begin()); // We want to remove the first char, since this is the wildcard's spot
                    add_completion_item(o.str(), "string", docs.str(), insert_text);
                }
            }
        }
        catch (semantic_error&){}
    }
}

void lsp_method_text_document_completion::complete(string code)
{
    size_t code_start = 0;
    // Skip magic lines [jupyter client specific]
    while (code.size() >= 2 && code[0] == '%' && code[1] == '%')
    {
        code_start = code.find('\n', code_start) + 1;
        code = code.substr(code_start);
    }

    /* Completion relies on parsing the code and seeing where there is a faliure
     * since the faliure point shows what is expected and what is wrong there
     * Pass 1 is called to determine the completion rule using:
     * - The context in which p1 failed
     * - Some information in particular about the failing token
    */ 
    const token *tok;
    pass_1(*lang_server->s, code, &tok);

    if (lang_server->verbose >= 2)
    {
        cerr << "Completion context: " << pc2str(lang_server->code_completion_context) << endl;
        cerr << "Code Block:" << code << endl;
    }

    switch (lang_server->code_completion_context)
    {
    case con_unknown:
        if (startswith(code, "private "))
            complete(code.substr(strlen("private ")));
        else
            for (string s : {"probe", "global", "private", "function"})
                if (startswith(s, code)) add_completion_item(s, "keyword");
        break;
    case con_probe:
    case con_function:
        if (in_body(code))
        {
            /* Completion of a function or probe body, which are the same */
            size_t body_start = code.find('{');
            string body = (body_start != code.size() - 1) ? code.substr(body_start + 1) : "";
            complete_body(body);
        }
        else if (lang_server->code_completion_context == con_probe && code.size() > strlen("probe"))
        {
            //  code is of the form 'probe...' up to and not incluing a possible body start ({, %{)
            string partial_sig = code.substr(strlen("probe") + 1);
            if (tok && tok->type == tok_junk && tok->junk_type == tok_junk_unclosed_quote)
                // The special case of string completion
                complete_string(partial_sig);
            else
                complete_probe_signature(partial_sig);
        }
        else
        {
            /* snippets of the form 'function ... {' don't have any sort of completion to do
                likewise neither do snippets of the form 'probe' */
        }
        break;
    case con_global:
    case con_embedded:
    default:
        /* snippets of the form global x = ... don't have any sort of completion to do*/
        /* TODO: For now we do not support C completion */
        break;
    }
    return;
}

jsonrpc_response *
lsp_method_text_document_completion::handle(json_object *p)
{
    lsp_object completion_params(p);

    doc = lang_server->wspace->get_document(completion_params.extract_substruct("textDocument").extract_string("uri"));

    insert_position = completion_params.extract_substruct("position");

    vector<string> lines = doc->get_lines();
    int cursor_line_num = insert_position.extract_int("line");

    // The line has the cursor on it so truncate at cursor pos
    lines.at(cursor_line_num) = lines.at(cursor_line_num).substr(0, insert_position.extract_int("character"));

    string completion_code_block;
    doc->get_last_code_block(lines, completion_code_block, cursor_line_num);

    lsp_object completion_list;
    // Allows the client to be more efficient, as it doesn't need to requery when another char is added, just filter with
    // the same completion list (Ex. completing 'pro' offers 'probe' so don't bother recomputing for 'prob' its the same)
    completion_list.insert("isIncomplete", false);
    complete(completion_code_block);
    completion_list.insert("items", completion_items);

    jsonrpc_response *res = new jsonrpc_response;
    res->set_result(completion_list.to_json());
    return res;
}