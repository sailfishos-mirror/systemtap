// Jsonrpc (client) implementation header
// Copyright (C) 2022 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.
// See: https://www.jsonrpc.org/specification

#ifndef JSONRPC_H
#define JSONRPC_H

#include <string>
#include <json-c/json.h>
#include <iostream>
#include <variant>
#include <unistd.h>
using namespace std;

static struct
{
    int ParseError = -32700;
    int InvalidRequest = -32600;
    int MethodNotFound = -32601;
    int InvalidParams = -32602;
    int InternalError = -32603;
    int ServerNotInitialized = -32002;
    int UnknownErrorCode = -32001;
} LSPErrCode;

struct jsonrpc_error : public runtime_error
{
private:
    json_object *error;

public:
    jsonrpc_error(int code, string message, json_object *data = NULL) : runtime_error(to_string(code) + "-" + message)
    {
        error = json_object_new_object();
        json_object_object_add(error, "code", json_object_new_int(code));
        json_object_object_add(error, "message", json_object_new_string(message.c_str()));
        if (data)
            json_object_object_add(error, "data", data);
    }

    json_object *get_json() { return error; }
};

struct jsonrpc_header
{
    size_t content_length;
    string content_type;

    jsonrpc_header() : content_length{0}, content_type{"application/vscode-jsonrpc; charset=utf-8"}
    {}
};

struct jsonrpc_request
{
    string jsonrpc;
    string method;
    json_object *params;
    variant<string, int> id;

    bool is_notification()
    {
        if (id.index() == 0) // id is a string
            return get<string>(id) == "";
        else // id is an int
            return get<int>(id) == -1;
    }

    jsonrpc_request(char *request_string)
    {
        json_object *request = json_tokener_parse(request_string);
        if (request)
        {
            jsonrpc = json_object_get_string(json_object_object_get(request, "jsonrpc"));
            method  = json_object_get_string(json_object_object_get(request, "method"));
            params  = json_object_object_get(request, "params");
            json_object *jid = json_object_object_get(request, "id");
            if (json_object_get_type(jid) == json_type_null)
                id = -1;
            else if (json_object_get_type(jid) == json_type_string)
                id = json_object_get_string(jid);
            else
                id = json_object_get_int(jid);
        }

        if (!request || jsonrpc != "2.0")
            throw jsonrpc_error(LSPErrCode.InvalidRequest, "");
    }
};

struct jsonrpc_response
{
private:
    json_object *payload;

public:
    bool result_or_error_set;

    jsonrpc_response()
    {
        payload = json_object_new_object();
        json_object_object_add(payload, "jsonrpc", json_object_new_string("2.0"));
        result_or_error_set = false;
    }

    bool set_error(json_object *e)
    {
        if (result_or_error_set)
            return false;
        json_object_object_add(payload, "error", e);
        result_or_error_set = true;
        return true;
    }

    bool set_result(json_object *r)
    {
        if (result_or_error_set)
            return false;
        json_object_object_add(payload, "result", r);
        result_or_error_set = true;
        return true;
    }

    json_object *to_json(jsonrpc_request *req)
    {
        if (!result_or_error_set)
            throw jsonrpc_error(LSPErrCode.InternalError, "Either result or error must be set on a json response");
        // If there was an error in detecting the id in the Request object (e.g. Parse error/Invalid Request), it MUST be Null.
        if (req->id.index() == 0)
        { // id is a string
            string id = get<string>(req->id);
            json_object_object_add(payload, "id", id != "" ? json_object_new_string(id.c_str()) : NULL);
        }else{ // id is an int
            int id = get<int>(req->id);
            json_object_object_add(payload, "id", id >= 0 ? json_object_new_int(id) : NULL);
        }
        return payload;
    }
};

class jsonrpc_connection
{
private:
    int IN_FILNO;
    int OUT_FILENO;
    int verbose;

    bool _read_header_line(string &line);
    void _read_header(jsonrpc_header &h);
    void _write_header_line(string field, string value, bool final_line = false);

public:
    jsonrpc_connection(int in_fd, int out_fd, int verbose) : IN_FILNO{in_fd}, OUT_FILENO{out_fd}, verbose{verbose}
    {
        // The in file-descriptor is a terminal
        if(1 == isatty(IN_FILNO)){
            cerr << "Warning: The language server should not be started in a terminal.\n" \
                 << "It should be run as the child of a LSP client" << endl;
            exit(1);
        }
    }

    // Client functions
    void wait_for_request();
    jsonrpc_request *get_request();
    void send_response(jsonrpc_request *request, jsonrpc_response *response);
    // TODO: Server functions send request/notification (needed only for completeness, not required)
};

#endif