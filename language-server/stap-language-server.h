// Language server header.
// Copyright (C) 2022 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.
// See: https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/

#ifndef LANGUAGE_SERVER_H
#define LANGUAGE_SERVER_H

#include "jsonrpc.h"
#include "session.h"
#include "parse.h"
#include "staptree.h"


#include <json-c/json.h>
#include <string>
#include <ostream>
#include <sstream>
#include <map>
#include <vector>
#include <functional>

using namespace std;

static struct
{
    int None = 0;        // Documents should not be synced at all
    int Full = 1;        // Documents are synced by always sending the full content of the document
    int Incremental = 2; // Documents are synced by sending incremental updates
} TextDocumentSyncKind;

class lsp_object
{
private:
    json_object *j_obj; // The internal json storage

    json_object *_to_json_object(string value)       { return json_object_new_string(value.c_str()); }
    json_object *_to_json_object(const char *value)  { return json_object_new_string(value); }
    json_object *_to_json_object(int value)          { return json_object_new_int(value); }
    json_object *_to_json_object(bool value)         { return json_object_new_boolean(value); }
    json_object *_to_json_object(unsigned int value) { return json_object_new_uint64(value); }
    json_object *_to_json_object(double value)       { return json_object_new_double(value); }
    json_object *_to_json_object(lsp_object value)   { return value.to_json(); }

    json_object *_get_and_check(string key)
    {
        json_object *value;
        if (!json_object_object_get_ex(j_obj, key.c_str(), &value))
            throw jsonrpc_error(LSPErrCode.InternalError, "Keyerror: " + key + " not found");
        return value;
    }

public:
    lsp_object(json_object *j_obj) : j_obj{j_obj}
    {}

    lsp_object() : j_obj{json_object_new_object()}
    {}

    json_object *to_json() { return j_obj; }

    const char  *extract_string(string key) { return json_object_get_string(_get_and_check(key)); }
    int          extract_int(string key)    { return json_object_get_int(_get_and_check(key)); }
    bool         extract_bool(string key)   { return json_object_get_boolean(_get_and_check(key)); }
    unsigned int extract_uint(string key)   { return json_object_get_uint64(_get_and_check(key)); }
    double       extract_double(string key) { return json_object_get_double(_get_and_check(key)); }

    template <typename T>
    vector<T> extract_array(string key)
    {
        json_object *arr = _get_and_check(key);
        vector<T> result;
        for (size_t i = 0; i < json_object_array_length(arr); i++)
            result.push_back(json_object_array_get_idx(arr, i));
        return result;
    }

    lsp_object extract_substruct(string key)
    {
        json_object *sub_struct = _get_and_check(key);
        return lsp_object(sub_struct);
    }

    void insert(string key, const char *value)  { json_object_object_add(j_obj, key.c_str(), _to_json_object(value)); }
    void insert(string key, int value)          { json_object_object_add(j_obj, key.c_str(), _to_json_object(value)); }
    void insert(string key, bool value)         { json_object_object_add(j_obj, key.c_str(), _to_json_object(value)); }
    void insert(string key, unsigned int value) { json_object_object_add(j_obj, key.c_str(), _to_json_object(value)); }
    void insert(string key, double value)       { json_object_object_add(j_obj, key.c_str(), _to_json_object(value)); }
    void insert(string key, string &value)      { json_object_object_add(j_obj, key.c_str(), _to_json_object(value.c_str())); }
    void insert(string key, lsp_object &value)  { json_object_object_add(j_obj, key.c_str(), _to_json_object(value)); }

    template <typename T>
    void insert(string key, vector<T> values)
    {
        json_object *arr = json_object_new_array_ext(values.size());
        for (auto v = values.begin(); v != values.end(); ++v)
            json_object_array_add(arr, _to_json_object(*v));
        json_object_object_add(j_obj, key.c_str(), arr);
    }

    lsp_object insert(string key)
    {
        // Calling insert without a value will create a substructre named key if one does already not exist
        // Otherwise it will return the existing structure
        // This allows for syntax like `foo.insert("bar").insert("baz", 42))` -> {"bar": {"baz": 42}}
        json_object *sub_struct = json_object_object_get(j_obj, key.c_str());
        if (sub_struct)
            return lsp_object(sub_struct);
        lsp_object obj;
        json_object_object_add(j_obj, key.c_str(), _to_json_object(obj));
        return obj;
    }
};

static string join_vector(vector<string> vec, string delim)
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

const token *
pass_1(systemtap_session &s, string &code);

int passes_0_4(systemtap_session &s);

class document
{
private:
    string uri;
    string source;
    systemtap_session* s;   

public:
    // globals/functions define within the document, which may be refrenced (ex. completion)
    vector<vardecl*> globals;
    map<string,functiondecl*> functions;

    document(string uri, systemtap_session* s, string source = "") : uri{uri}, source{source}, s{s}
    {}

    vector<string> get_lines()
    {
        vector<string> lines;
        stringstream ss = stringstream(source);
        for (string line; getline(ss, line, '\n');)
            lines.push_back(line);
        return lines;
    }

    /* Get the last code_block before last_line
     * Return the start lin number of the code_block
     */
    int get_last_code_block(vector<string>& lines, string& code, int last_line){
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

    void register_code_block(string& code){
        (void)pass_1(*s, code);

        stapfile *user_file = s->user_files.back();
        if(user_file){ // Just skip if there is an error is a code blocks
            globals.insert(globals.end(), user_file->globals.begin(), user_file->globals.end());
            for(auto f = user_file->functions.begin(); f != user_file->functions.end(); ++f)
                functions.insert({ (*f)->name, *f });
            // TODO: Macros, user_file->aliases
        }
    }

    void register_code_blocks(){
        functions.clear();
        globals.clear();
        // TODO: Macros, user_file->aliases

        vector<string> lines = get_lines();
        string code;
        int line_num = lines.size();
        while(0 <= (line_num = get_last_code_block(lines, code, line_num-1)))
            register_code_block(code);
    }

    void apply_change(lsp_object change)
    {
        // Apply a text change to a document
        // NOTE: The TextDocumentSyncKind is Full, meaning that for any change the entire
        // document's content (this->source) is replaced
        // TODO: If needed an optimization would be to change this to Incremental
        source = change.extract_string("text");
        register_code_blocks();
        // update the decls
    }
};

class workspace
{
private:
    map<string, document*> docs;
    systemtap_session* s;

    document *create_document(string doc_uri, string source = "")
    {
        return new document(doc_uri, s, source);
    }

public:
    workspace(systemtap_session* s) : s{s} {}

    document *get_document(string doc_uri)
    {
        // Get the document, or create one if none are found
        auto d = docs.find(doc_uri);

        if (d == docs.end())
        {
            document *doc = create_document(doc_uri);
            docs.insert({doc_uri, doc});
            return doc;
        }
        return d->second;
    }

    void add_document(lsp_object text_document)
    {
        string doc_uri = text_document.extract_string("uri");
        docs.insert({doc_uri, create_document(doc_uri, text_document.extract_string("text"))});
    }

    void remove_document(string doc_uri)
    {
        delete get_document(doc_uri);
        docs.erase(doc_uri);
    }

    void update_document(lsp_object text_doc, lsp_object change)
    {
        string doc_uri = text_doc.extract_string("uri");
        get_document(doc_uri)->apply_change(change);
    }
};

class language_server
{
private:
    map<string, function<jsonrpc_response *(language_server *ls, json_object *p)>> registered_lsp_methods;
    jsonrpc_response *handle_lsp_method(jsonrpc_request *req);
    
    void register_lsp_method(string method, function<jsonrpc_response *(language_server *ls, json_object *p)> handler)
    { registered_lsp_methods.insert({method, handler}); }

public:
    language_server(systemtap_session *s, unsigned int verbose);
    int run();

    systemtap_session *s;
    unsigned int verbose;
    workspace *wspace;
    bool running;
    bool init_request_received;
    bool shutdown_request_received;

    // Some data needed for completion requests
    parse_context code_completion_context;
    const token *code_completion_target;

    bool is_registered(string method) { return registered_lsp_methods.find(method) != registered_lsp_methods.end(); }

    // Throws jsonrpc_error if condition is false 
    void is_method_undefined(jsonrpc_request *req);
    void is_premature_request(jsonrpc_request *req);
    void is_already_shutdown(jsonrpc_request *req);
    void is_duplicate_initialization(jsonrpc_request *req);
};

class lsp_method
{
protected:
    language_server *lang_server;

public:
    lsp_method(language_server *lang_server) : lang_server{lang_server}
    {}

    virtual jsonrpc_response *handle(json_object *)
    { return nullptr; }

    template <typename T>
    static jsonrpc_response *handler(language_server *lang_server, json_object *params)
    {
        T t(lang_server);
        return t.handle(params);
    }
};

class lsp_method_text_document_completion : public lsp_method
{
private:
    vector<lsp_object> completion_items;
    lsp_object insert_position;
    document *doc;

    // Helper methods
    void add_completion_item(string text, string type = "", string docs = "", string insert_text = "");
    void complete_body(string &partial_statement);
    void complete_probe_signature(string &partial_signature);
    void complete_path(string path);
    void complete_string(string &partial_signature);
    void complete(string code);

public:
    lsp_method_text_document_completion(language_server *lang_server) : lsp_method(lang_server) {}
    inline static const string TEXT_DOCUMENT_COMPLETION = "textDocument/completion";
    jsonrpc_response *handle(json_object *p);
};

#endif
