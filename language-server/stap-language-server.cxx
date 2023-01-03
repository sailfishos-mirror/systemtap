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
#include "staptree.h"
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

const token *
pass_1(systemtap_session &s, string &code)
{
    // Set up session to only run pass 1.
    s.last_pass = 1;
    s.monitor = false;

    // Add the code snippet
    s.cmdline_script = code;
    s.have_script = true;

    // Ignore the output since it will probably raise a parse error
    ofstream file("/dev/null");
    streambuf *former_cout = cout.rdbuf(file.rdbuf());
    streambuf *former_cerr = cerr.rdbuf(file.rdbuf());
    const token *tok = NULL;
    try
    {
        passes_0_4(s);
    }
    catch (const parse_error &pe)
    {
        tok = pe.tok ? pe.tok : s.language_server->code_completion_target;
    }
    cout.rdbuf(former_cout);
    cerr.rdbuf(former_cerr);
    file.close();
    return tok;
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
        lang_server->wspace = new workspace;

        lsp_object server_capabilities;
        // The server currently only supports Full synchronization
        // i.e changes will replace the current content of the document with a full new version
        server_capabilities.insert("textDocumentSync", TextDocumentSyncKind.Full);

        if (lang_server->is_registered(lsp_method_text_document_completion::TEXT_DOCUMENT_COMPLETION))
        {
            lsp_object completion_options;
            completion_options.insert("resolveProvider", false);
            /* NB: It is worth mentioned that some LS clients will refer to triggerCharacters
             * when determining the position to insert the completion (Ex. vim-lsp).
             * This means that if for example completing @foobar results in unexpected insertions
             * it may be worth considering if '@' should be a trigger char
             */
            completion_options.insert<string>("triggerCharacters", {".", "/", "@"});
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
        // NOTE: The language server currently only supports TextDocumentSyncKind.Full changes
        // Since only full changes occur, the last one is the only one of relevance
        lang_server->wspace->update_document(
            did_change_text_document_params.extract_substruct("textDocument"),
            did_change_text_document_params.extract_array<lsp_object>("contentChanges").back());
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
    (void)pass_1(*this->s, empty);

    this->s->register_library_aliases();
    register_standard_tapsets(*this->s);
    // TODO: The current document will also hold function/globals which should be considered for completion. Currently ignored
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
