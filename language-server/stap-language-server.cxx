// Systemtap language server
// Copyright (C) 2022 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.
#include "stap-language-server.h"
#include "jsonrpc.h"
#include "config.h"
#include "parse.h"
#include "session.h"
#include "tapsets.h"
#include "util.h"

#include <json-c/json.h>
#include <ext/stdio_filebuf.h>

using namespace std;

static void
transcribe_functions_and_globals(systemtap_session &s)
{
    for (auto f = s.library_files.begin(); f != s.library_files.end(); ++f)
    {
        for (auto it = (*f)->functions.begin(); it != (*f)->functions.end(); ++it)
        {
            functiondecl *v = *it;
            if (v->synthetic || !startswith(v->name, "__global_"))
                continue;
            s.functions[v->name] = v;
        }

        for (auto it = (*f)->globals.begin(); it != (*f)->globals.end(); ++it)
        {
            vardecl *v = *it;
            if (v->synthetic || !startswith(v->name, "__global_"))
                continue;
            s.globals.push_back(v);
        }
    }
}

int
pass_1(systemtap_session &s, string &code, const token **tok)
{
    // Set up session to only run pass 1.
    s.last_pass = 1;
    s.monitor = false;
    s.seen_errors.clear(); // The pass is standalone
    s.warningerr_count = 0;

    // Add the code snippet
    s.cmdline_script = code;
    s.have_script = true;

    // Ignore the output since it will probably raise a parse error
    ofstream file("/dev/null");
    streambuf *former_cout = cout.rdbuf(file.rdbuf());
    streambuf *former_cerr = cerr.rdbuf(file.rdbuf());
    int rc = 0;
    try
    {
        if(tok) *tok = NULL;
        rc = passes_0_4(s);
    }
    catch (const parse_error &pe)
    {
        if(tok)
            *tok = pe.tok ? pe.tok : s.language_server->code_completion_target;
        rc = 1;
    }
    cout.rdbuf(former_cout);
    cerr.rdbuf(former_cerr);
    file.close();
    return rc;
}

string
join_vector(vector<string> vec, string delim)
{
    stringstream result;
    for (auto it = vec.begin(); it != vec.end(); ++it)
    {
        result << *it;
        if (it != vec.end() - 1)
            result << delim;
    }
    return result.str();
}

vector<string>
document::get_lines(){
    vector<string> lines;
    stringstream ss = stringstream(source);
    for (string line; getline(ss, line, '\n');) //FIXME: Does this respect newlines in strings
        lines.push_back(line);
    return lines;
}

int
document::get_last_code_block(vector<string>& lines, string& code, int last_line){
    static vector<string> start_tokens = {"probe", "function", "global", "private"};

    // NB: For the right-strip, DON'T remove spaces since token seperation requires it ex. "probe" vs "probe "
    static auto strip = [](string s)
        { return s.erase(0, s.find_first_not_of("\t\n\r ")).erase(s.find_last_not_of("\t\n\r") + 1); };

    vector<string> block;
    int i = last_line;
    // Keep appending lines until a valid start token (or at least the start of one) is found.
    for (; i >= 0; i--)
    {
        string line = strip(lines[i]);
        block.push_back(line); // The last step will be to reverse the whole vector and merge it
        vector<string> words;
        tokenize(line, words, " ");
        if (words.size() > 0 && any_of(start_tokens.cbegin(), start_tokens.cend(), [words](string s_token)
                                    { return startswith(s_token, words[0]); }))
            break; // Found the context start (the absolute start is handled by the loop condition itself)
    }
    reverse(block.begin(), block.end());
    code = join_vector(block, "\n");
    return i;
}

void
document::register_code_block(string& code, int first_line){
    // Clear old user_files, since each registeration should be independant
    for(auto uf = s->user_files.cbegin(); uf != s->user_files.cend(); uf = s->user_files.erase(uf) )
        delete *uf;

    // Parse the code_block, which is made relative to the document start.
    // This is needed since in order to determine the current valid definitions,
    // we need to see the absolute line numbers on which they were created
    string relativised = string(first_line, '\n') + code;
    int rc = pass_1(*s, relativised , NULL);

    if(0 == rc){ // Just skip if there is an error in a code blocks
        stapfile *user_file = s->user_files.front();
        globals.insert(globals.end(), user_file->globals.begin(), user_file->globals.end());
        for(auto f = user_file->functions.begin(); f != user_file->functions.end(); ++f)
            functions.insert({ (*f)->name, *f });
        // TODO: Probe aliases (user_file->aliases)
    }
}

void 
document::register_code_blocks(int start_line, bool clear_decls){
    if(clear_decls)
    {
        for(auto g = globals.cbegin(); g != globals.cend(); g = globals.erase(g) )
            delete *g;
        for(auto f = functions.cbegin(); f != functions.cend(); f = functions.erase(f) )
            delete f->second;
        // TODO: Probe aliases (user_file->aliases)
    }

    vector<string> lines = get_lines();
    string code;
    int line_num = lines.size();
    // NB: When registering the code blocks we want to parse as normal
    // not exit early when we find an error (as we would for completion).
    // So we temporarily disable language_server_mode
    s->language_server_mode = false;
    while(start_line <= (line_num = get_last_code_block(lines, code, line_num-1)))
        register_code_block(code, line_num);
    s->language_server_mode = true;
}

void
document::apply_change(lsp_object change, TextDocumentSyncKind kind){
    string text = change.extract_string("text");
    switch (kind)
    {
    case None:
        break; // Do Nothing
    case Full:
        source = text;
        register_code_blocks();
        break;
    case Incremental:
        vector<size_t> line_starts;
        
        lsp_object range(change.extract_substruct("range"));
        size_t start_ln  = range.extract_substruct("start").extract_int("line");
        size_t start_col = range.extract_substruct("start").extract_int("character");
        size_t end_ln    = range.extract_substruct("end").extract_int("line");
        size_t end_col   = range.extract_substruct("end").extract_int("character");

        // Determine the index at which each line begins, so we can do an inplace updating of the source
        for(size_t idx = 0; source.npos != idx; idx = source.find('\n', idx+1))
            line_starts.push_back(idx == 0 ? 0 : idx + 1); // We want the character to the right of \n (except for the first char)

        // Check for an edit occurring at the very end of the file
        if (start_ln == line_starts.size()){
            source += text;
        }else{
            size_t insert_range_start  = line_starts[start_ln] + start_col;
            size_t insert_range_end    = line_starts[end_ln] + end_col;
            
            // Cleared the range (which can be empty i.e start=end ==> insertion with no replacement) and insert into that gap
            source.erase(insert_range_start, insert_range_end-insert_range_start);
            source.insert(insert_range_start, text);

            // Remove and reregister the remaining code blocks from start_ln to the end.
            // These are the ones which may have been changed by an insertion
            // since they may either be directly modified, or at the very least
            // their source locations will be off (as they may be shifted by an insertion)
            for (auto it = globals.cbegin(); it != globals.cend();)
            {
                if (start_ln <= (*it)->tok->location.line - 1){
                    delete *it;
                    it = globals.erase(it);
                }
                else
                    ++it;
            }
            for (auto it = functions.cbegin(); it != functions.cend();)
            {
                if (start_ln <= it->second->tok->location.line - 1){
                    delete it->second;
                    it = functions.erase(it);
                }
                else
                    ++it;
            }
        }
        register_code_blocks(start_ln /* Only reregister up to start_ln */ , false /* Don't clear the vectors */ );
        break;
    }
}

/* The initialize request is sent from the client to the server.
 * It is sent once as the request after starting up the server.
 * The requests parameter is of type [InitializeParams](#InitializeParams)
 * the response if of type [InitializeResult](#InitializeResult) of a Thenable that\nresolves to such.
 */
class lsp_method_initialize : public lsp_method
{
public:
    lsp_method_initialize(language_server *lang_server) : lsp_method(lang_server) {}
    inline static const string INITIALIZE = "initialize";

    jsonrpc_response *handle(json_object *p)
    {
        lsp_object initialize_params(p);

        lang_server->init_request_received = true;
        lang_server->wspace = new workspace(lang_server->s);

        lsp_object server_capabilities;
        // Documents are synced by sending the full content on open.
        // After that only incremental updates to the document are sent.
        server_capabilities.insert("textDocumentSync", Incremental);

        if (lang_server->is_registered(lsp_method_text_document_completion::TEXT_DOCUMENT_COMPLETION))
        {
            lsp_object completion_options;
            completion_options.insert("resolveProvider", false);
            /* NB: It is worth mentioned that some LS clients will refer to triggerCharacters
             * when determining the position to insert the completion (Ex. vim-lsp).
             * This means that if for example completing @foobar results in unexpected insertions
             * it may be worth considering if '@' should be a trigger char
             */
            completion_options.insert<string>("triggerCharacters", {".", "/", "@", "*"});
            server_capabilities.insert("completionProvider", completion_options);
        }

        lsp_object initialize_result;
        initialize_result.insert("capabilities", server_capabilities);
        initialize_result.insert("serverInfo").insert("name", "systemtap-language-server");
        initialize_result.insert("serverInfo").insert("version", "1.0");
        jsonrpc_response *res = new jsonrpc_response;
        res->set_result(initialize_result.to_json());
        return res;
    }
};

/* The initialized notification is sent from the client to the
 * server after the client is fully initialized and the server
 * is allowed to send requests from the server to the client
 */
class lsp_method_initialized : public lsp_method
{
public:
    lsp_method_initialized(language_server *lang_server) : lsp_method(lang_server) {}
    inline static const string INITIALIZED = "initialized";
    // Pass, no action required
};

/* The document open notification is sent from the client to the server to signal
 * newly opened text documents. The document is now managed by the client
 */
class lsp_method_text_document_did_open : public lsp_method
{
public:
    lsp_method_text_document_did_open(language_server *lang_server) : lsp_method(lang_server) {}
    inline static const string TEXT_DOCUMENT_DID_OPEN = "textDocument/didOpen";

    jsonrpc_response *handle(json_object *p)
    {
        lsp_object did_open_text_document_params(p);
        lang_server->wspace->add_document(did_open_text_document_params.extract_substruct("textDocument"));
        return nullptr;
    }
};

/* The document change notification is sent from the client to the server to signal
 * changes to a text document.
 */
class lsp_method_text_document_did_change : public lsp_method
{
public:
    lsp_method_text_document_did_change(language_server *lang_server) : lsp_method(lang_server) {}
    inline static const string TEXT_DOCUMENT_DID_CHANGE = "textDocument/didChange";

    jsonrpc_response *handle(json_object *p)
    {
        lsp_object did_change_text_document_params(p);
        // Updates document's content.
        lsp_object text_document  (did_change_text_document_params.extract_substruct("textDocument"));
        vector<lsp_object> content_changes(did_change_text_document_params.extract_array<lsp_object>("contentChanges"));

        for(auto change = content_changes.begin(); change != content_changes.end(); ++change){
            // If given a range then we can do an incremental change, otherwise just do a full (replace the entire
            // copy of the doc source with the new given one)
            TextDocumentSyncKind sync_kind = change->contains("range") ? Incremental : Full;
            lang_server->wspace->update_document(
            text_document, *change, sync_kind);
        }
        return nullptr;
    }
};

/* The document close notification is sent from the client to the server when
 * the document got closed in the client.
 */
class lsp_method_text_document_did_close : public lsp_method
{
public:
    lsp_method_text_document_did_close(language_server *lang_server) : lsp_method(lang_server) {}
    inline static const string TEXT_DOCUMENT_DID_CLOSE = "textDocument/didClose";

    jsonrpc_response *handle(json_object *p)
    {
        lsp_object did_close_text_document_params(p);
        lang_server->wspace->remove_document(did_close_text_document_params.extract_substruct("textDocument").extract_string("uri"));
        return nullptr;
    }
};

/* The document save notification is sent from the client to the server when
 * the document got saved in the client
 */
class lsp_method_text_document_did_save : public lsp_method
{
public:
    lsp_method_text_document_did_save(language_server *lang_server) : lsp_method(lang_server) {}
    inline static const string TEXT_DOCUMENT_DID_SAVE = "textDocument/didSave";
    // Pass, no action required
};

/* A shutdown request is sent from the client to the server.
 * It is sent once when the client decides to shutdown the
 * server. The only notification that is sent after a shutdown request
 * is the exit event.
 */
class lsp_method_shutdown : public lsp_method
{
public:
    lsp_method_shutdown(language_server *lang_server) : lsp_method(lang_server) {}
    inline static const string SHUTDOWN = "shutdown";

    jsonrpc_response *handle(json_object *)
    {
        lang_server->shutdown_request_received = true;
        return nullptr;
    }
};

/* The exit event is sent from the client to the server to
 * ask the server to exit its process
 */
class lsp_method_exit : public lsp_method
{
public:
    lsp_method_exit(language_server *lang_server) : lsp_method(lang_server) {}
    inline static const string EXIT = "exit";

    jsonrpc_response *handle(json_object *)
    {
        // Exit should only be called before a server is initialized or after a shutdown
        if (lang_server->init_request_received && !lang_server->shutdown_request_received)
            exit(1);

        delete lang_server->wspace;
        lang_server->running = false;
        return nullptr;
        // The mainloop of the language server, controlled by running, will end and the process
        // will end after cleanup
    }
};

void
language_server::is_method_undefined(jsonrpc_request *req)
{
    if (req->method.empty())
        throw jsonrpc_error(LSPErrCode.MethodNotFound, "Method not specified");
}

void
language_server::is_premature_request(jsonrpc_request *req)
{
    if (!init_request_received && req->method != lsp_method_initialize::INITIALIZE && req->method != lsp_method_exit::EXIT)
        throw jsonrpc_error(LSPErrCode.ServerNotInitialized, "Client sent a request before the server was initialized");
}

void
language_server::is_already_shutdown(jsonrpc_request *req)
{
    if (shutdown_request_received && req->method != lsp_method_exit::EXIT)
        throw jsonrpc_error(LSPErrCode.InvalidRequest, "Client sent a non-exit request/notification after shutdown");
}

void
language_server::is_duplicate_initialization(jsonrpc_request *req)
{
    if (init_request_received && req->method == lsp_method_initialize::INITIALIZE)
        throw jsonrpc_error(LSPErrCode.InvalidRequest, "Client sent duplicate initialization");
}

jsonrpc_response *
language_server::handle_lsp_method(jsonrpc_request *req)
{
    string method = req->method;

    // If any of the following are true, throw an expection (caught in the main loop)
    is_method_undefined(req);
    is_premature_request(req);
    is_duplicate_initialization(req);
    is_already_shutdown(req);

    auto it = registered_lsp_methods.find(method);
    if (it == registered_lsp_methods.end())
        throw jsonrpc_error(LSPErrCode.MethodNotFound, "Unknown method: " + method);
    // Return the response of the handle function for this particular lsp_method_* class instance
    return it->second(this, req->params);
}

language_server::language_server(systemtap_session *sess, unsigned int v) : s{sess}, verbose{v}
{
    running = init_request_received = shutdown_request_received = false;

    // When registering a method, a map of a method name to handlers is created
    // where the handler<T> will make a new INSTANCE of T, t, and then handle it with the given parameters
    // It is therefore safe to assume that all attributes of t are fresh for each method call

    // Lifecycle messages
    register_lsp_method(lsp_method_initialize::INITIALIZE,   lsp_method::handler<lsp_method_initialize>);
    register_lsp_method(lsp_method_initialized::INITIALIZED, lsp_method::handler<lsp_method_initialized>);
    register_lsp_method(lsp_method_shutdown::SHUTDOWN,       lsp_method::handler<lsp_method_shutdown>);
    register_lsp_method(lsp_method_exit::EXIT,               lsp_method::handler<lsp_method_exit>);

    // Document Synchronization
    register_lsp_method(lsp_method_text_document_did_open::TEXT_DOCUMENT_DID_OPEN,     lsp_method::handler<lsp_method_text_document_did_open>);
    register_lsp_method(lsp_method_text_document_did_change::TEXT_DOCUMENT_DID_CHANGE, lsp_method::handler<lsp_method_text_document_did_change>);
    register_lsp_method(lsp_method_text_document_did_save::TEXT_DOCUMENT_DID_SAVE,     lsp_method::handler<lsp_method_text_document_did_save>);
    register_lsp_method(lsp_method_text_document_did_close::TEXT_DOCUMENT_DID_CLOSE,   lsp_method::handler<lsp_method_text_document_did_close>);

    // Language Features
    register_lsp_method(lsp_method_text_document_completion::TEXT_DOCUMENT_COMPLETION, lsp_method::handler<lsp_method_text_document_completion>);
    code_completion_context = con_unknown;
    code_completion_target = NULL;
}

int language_server::run()
{
    // The first time pass_1a is completed, we parse all the library scripts, so do it upon init to remove an initial delay
    string empty = "";
    pass_1(*this->s, empty, NULL);
    this->s->register_library_aliases();
    register_standard_tapsets(*this->s);
    transcribe_functions_and_globals(*this->s);

    // TODO: Add TCP option as well, for now just default to stdio
    jsonrpc_connection conn(STDIN_FILENO, STDOUT_FILENO, verbose);

    running = true;
    while (running)
    {
        conn.wait_for_request();
        unique_ptr<jsonrpc_request> request;
        unique_ptr<jsonrpc_response> response;

        try
        {
            request = unique_ptr<jsonrpc_request>(conn.get_request());
        }
        catch (jsonrpc_error &e)
        {
            // If the request parse process fails, e.g.
            // % stap --language-server << /dev/null
            if (verbose)
                cerr << "Error: " << e.what() << endl;
            running = false; // shut things down
            continue;
        }
        
        if (verbose)
          cerr << "---> " << request->method << "(" << (request->is_notification() ? "NOTIF" : "REQ") << ")" << json_object_to_json_string(request->params) << endl;

        try
        {
            response = unique_ptr<jsonrpc_response>(this->handle_lsp_method(request.get()));
        }
        catch (jsonrpc_error &e)
        {
            if (verbose)
                cerr << "Error: " << e.what() << endl;
            response = unique_ptr<jsonrpc_response>(new jsonrpc_response());
            response->set_error(e.get_json());
        }

        if (!request->is_notification() && response && response->result_or_error_set)
        {
            if (verbose)
                cerr << "<--- " << json_object_to_json_string(response->to_json(request.get())) << endl;
            conn.send_response(request.get(), response.get());
        }
    }
    return 0;
}
