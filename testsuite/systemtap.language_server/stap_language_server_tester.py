# Language server tests
# Copyright (C) 2022 Red Hat Inc.
#
# This file is part of systemtap, and is free software.  You can
# redistribute it and/or modify it under the terms of the GNU General
# Public License (GPL); either version 2, or (at your option) any
# later version.
from jsonrpc import ReadWriter, JSONRPC2Connection
import sys
import os
import argparse
import unittest
import subprocess
from mock import mock
from enum import StrEnum

class Method(StrEnum):
    # General
    EXIT = 'exit'
    INITIALIZE = 'initialize'
    INITIALIZED = 'initialized'
    SHUTDOWN = 'shutdown'
    TEXT_DOCUMENT_DID_CHANGE = 'textDocument/didChange'
    TEXT_DOCUMENT_DID_CLOSE = 'textDocument/didClose'
    TEXT_DOCUMENT_DID_OPEN = 'textDocument/didOpen'
    TEXT_DOCUMENT_DID_SAVE = 'textDocument/didSave'
    # Language Features
    COMPLETION = 'textDocument/completion'


class MockClient():
    def __init__(self):
        self.server_conn = None
        verbose = CMD_ARGS.verbose

        args = [CMD_ARGS.stap_path, '--language-server']
        if verbose > 0:
            args.extend(['-'+'v'*verbose])
        server = subprocess.Popen(
            args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=False)
        self.server_conn = JSONRPC2Connection(
            ReadWriter(reader=server.stdout, writer=server.stdin))

        # Setup and open a doccument

        _ = self.server_conn.send_request(
            method=Method.INITIALIZE.value,
            params=dict(
                rootUri=f'file://{os.getcwd()}',
                capabilities=dict(),
                rootPath=os.getcwd(),
                clientInfo=dict(
                    name='test-language-server-client',
                    processId=os.getpid(),
                    trace='off'
                )
            )
        )

        self.server_conn.send_notification(Method.INITIALIZED.value, dict())

        self.text_document = dict(
            uri='file://fake_doc.txt',
            languageId='systemtap',
            version=1,
            text=''
        )

        self.server_conn.send_notification(
            method=Method.TEXT_DOCUMENT_DID_OPEN.value,
            params=dict(textDocument=self.text_document)
        )

    def __del__(self):
        # Close and showdown

        self.server_conn.send_notification(
            method=Method.TEXT_DOCUMENT_DID_CLOSE,
            params=dict(textDocument=dict(uri=self.text_document['uri']))
        )

        self.server_conn.send_notification(Method.SHUTDOWN.value, dict())

        self.server_conn.send_notification(Method.EXIT.value, dict())

    def completion_request(self, code_snippet, position=None):
        # If position is not provided, assume we're at the snippet end
        if not position:
            lines = code_snippet.split("\n")
            position = dict(line=len(lines)-1, character=len(lines[-1]))

        self.server_conn.send_notification(
            method=Method.TEXT_DOCUMENT_DID_CHANGE.value,
            params=dict(
                textDocument=dict(
                    uri=self.text_document['uri'],
                    version=1
                ),
                contentChanges=[dict(
                    text=code_snippet,
                )]
            )
        )

        return self.server_conn.send_request(
            method=Method.COMPLETION.value,
            params=dict(
                textDocument=dict(uri=self.text_document['uri']),
                position=position
            )
        )


class ServerCompletionTests(unittest.TestCase):
    def __init__(self, *args, **kwargs) -> None:
        super().__init__(*args, **kwargs)
        self.client = MockClient() # Each testcase gets its own mockclient

    def labels_match(self, code_block, expected, position=None, exactMatch=True, type=None):
        completion_list = self.client.completion_request(
            code_block, position).get("result", dict())

        completion_items = completion_list.get("items", [])

        # Get the labels (the default insertion text) of type detail (if a type is provided, else everything)
        actual = [ci["label"]
                  for ci in completion_items if not type or ci["detail"] == type]

        # Expected is a subset of actual
        subset_match = all(e in actual for e in expected)
        # We may be ok with just having a subset match, or we might need an exact match
        length_match = not exactMatch or len(actual) == len(expected)

        self.assertTrue(subset_match and length_match,
                        f'\nCompleting "{code_block}"\nGot: {actual}\nExpected: {expected}')

    def test_basic(self):
        # General completions for an unknown context
        self.labels_match('pr', ['probe', 'private'])
        self.labels_match('func', ['function'])
        self.labels_match('g', ['global'])
        # the private prefix just skips private and completes like normal
        self.labels_match('private pro', ['probe'])
        # Some text/whitespace before the completion doesn't change anything
        self.labels_match('\t\n\n       p', ['probe', 'private'])
        self.labels_match(
            '%%edit foobar\t    \n\n  function foo ()\n{}\n     globa', ['global'])

    def test_probe_comps(self):
        self.labels_match('probe', [])
        # Uses match nodes correctly
        self.labels_match('probe one', ['oneshot'])
        self.labels_match('probe proces', ['process', 'process(', 'process("'])
        # Completing a second token
        self.labels_match('probe process', [
                          'process', 'process(', 'process("'])
        # Just look for a subset of expected in actual
        self.labels_match('probe process.', ['begin'], exactMatch=False)
        # Doesn't match anything
        self.labels_match('probe twosh', [])
        self.labels_match('probe process(12345678).en', ['end'])

    def test_string(self):
        file_path = os.getenv("TEST_C_FILE")

        process_prefix = 'probe process("'
        # Absolute path completion
        self.labels_match(process_prefix + '/', ['/root'], exactMatch=False)
        self.labels_match(process_prefix + '/li', ['/lib', '/lib64'])
        # Relative path completion
        self.labels_match(process_prefix + 'foo/', ['bar'])
        self.labels_match(process_prefix + 'foo/bar/', ['baz', 'baz.c'])

        # Check the text to be inserted as well, not just the label (textEdit has the same content as insertText so it's covered by this test as well)
        insert_text_test_code = process_prefix + 'foo/ba'
        actual = [(ci["label"], 'foo/ba'+ci["insertText"]) for ci in self.client.completion_request(
            insert_text_test_code).get("result", dict(items=[]))["items"]]
        expected = [('bar', 'foo/bar')] # = (label, prefix + insertion text)
        self.assertTrue(all(e in actual for e in expected) and len(actual) == len(expected),
            f'\nCompleting "{insert_text_test_code}"\nGot: {actual}\nExpected: {expected}')
        # TODO: Test with globs once they're supported

        # Test function/statement completion using absolute path
        self.labels_match(f'probe process("{os.getcwd().replace("build", "systemtap")}/systemtap.language_server/foo/bar/baz").function("s',
                          [f'"spam@{file_path}:1"',
                           f'"space@{file_path}:5"'])

    def test_body(self):
        prefix = "probe oneshot, foo, bar, baz { \n"
        # Body completion does not determine the structure, it will just return the available key/atwords/macros/functions
        # first line testing
        self.labels_match(prefix + 'fo', ['for', 'foreach'], type="keyword")
        self.labels_match(
            prefix + '@pr', ['probewrite', 'prometheus_dump_array1'], exactMatch=False)
        self.labels_match(
            prefix + 'ti', ['tid', 'timer_pending'], type="function")

        # Complex line testing
        line = "\t\tif ( cpu() == 0 && getti<CURSOR> > 1140498000)"
        pos = dict(line=1, character=line.strip().find("<CURSOR>"))
        self.labels_match(prefix + line,
                          ['gettimeofday_ms', 'gettimeofday_ns', 'gettimeofday_s', 'gettimeofday_us'], position=pos)

        line = "foreach ([a,b] in foo+ lim"
        self.labels_match(prefix + line, ['limit'])

        # multiline testing
        lines = """
        function foo (a, b, c)
        {
            if (a < 1) return 0
            else if (b < 2) return 1
            else return modu
        """
        self.labels_match(lines, ['module_name', 'module_size'])

    def test_multiple_contexts(self):
        lines = """
        global baz = 42
        
        probe syscallgroup.io = syscall.open,
               syscall.read { groupname = "io" }
        function f(x) { printf("%s\n", x) }
        probe    end
        {
            f("hello world");
        foreach (eg+ in groups)
        @coun
        """
        self.labels_match(lines, ['count'])

    def test_no_completions(self):
        # Global defintions, function signatures and embedded C have no completions
        # TODO: Complete C
        self.labels_match("global x = ", [])
        self.labels_match("function foo (a, ", [])
        self.labels_match("probe", [])
        self.labels_match("function foo () %{ struc ", [])


class ServerIntegrationTests(unittest.TestCase):
    def __init__(self, *args, **kwargs) -> None:
        super().__init__(*args, **kwargs)
        self.verbose = CMD_ARGS.verbose
        self.server_conn = None

    def _test_request(self, request_method: Method, request_params: dict, expected_result: dict):
        response = self.server_conn.send_request(
            request_method.value, request_params)
        self.assertDictEqual(response.get("result", {}), expected_result)

    def _send_notification(self, notification_method: Method, notification_params: dict = dict()):
        self.server_conn.send_notification(
            notification_method.value, notification_params)

    def test_basic(self):
        args = [CMD_ARGS.stap_path, '--language-server']
        if self.verbose > 0:
            args.extend(['-'+'v'*self.verbose])
        server = subprocess.Popen(
            args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=False)
        self.server_conn = JSONRPC2Connection(
            ReadWriter(reader=server.stdout, writer=server.stdin))

        # Request that the server initialize
        self._test_request(
            request_method=Method.INITIALIZE,
            request_params=dict(
                rootUri=f'file://{os.getcwd()}',
                capabilities=dict(),
                rootPath=os.getcwd(),
                clientInfo=dict(
                    name='test-language-server-client',
                    processId=os.getpid(),
                    trace='off'
                )
            ),
            expected_result=dict(
                capabilities=dict(
                    textDocumentSync=1, # Full Sync
                    completionProvider=dict(
                        resolveProvider=False,
                        triggerCharacters=['.', '/', '@']
                    )
                ),
                serverInfo=dict(
                    name="systemtap-language-server",
                    version="1.0"
                )
            )
        )

        # Send the server a notification that the initialization was successful.
        # NOTE: Notifications don't have results, we just check their side effects to ensure
        # correctness
        self._send_notification(notification_method=Method.INITIALIZED)

        textDocument = dict(
            uri='file://fake_doc.txt',
            languageId='systemtap',
            version=1,
            text='pr'
        )

        self._send_notification(
            notification_method=Method.TEXT_DOCUMENT_DID_OPEN,
            notification_params=dict(
                textDocument=textDocument
            )
        )

        # By changing the textDocument's text from pr to pro the completion request verifies
        # the change was correct since only 'probe' should be returned, not 'private' as well
        self._send_notification(
            notification_method=Method.TEXT_DOCUMENT_DID_CHANGE,
            notification_params=dict(
                textDocument=dict(
                    uri=textDocument['uri'],
                    version=textDocument['version']+1
                ),
                contentChanges=[dict(
                    text=textDocument['text'] + 'o',
                )]
            )
        )

        # Make sure the unknown methods don't cause issues
        response = self.server_conn.send_request(
            "foobar", dict())
        MethodNotFoundError = -32601
        self.assertEqual(response.get("error", {}).get(
            "code", -1), MethodNotFoundError)

        self._test_request(
            request_method=Method.COMPLETION,
            request_params=dict(
                textDocument=dict(
                    uri=textDocument['uri']
                ),
                position=dict(
                    character=3,
                    line=0
                )
            ),
            expected_result=dict(
                isIncomplete=False,
                items=[dict(
                    detail='keyword',
                    label='probe'
                )]
            )
        )

        self._send_notification(
            notification_method=Method.TEXT_DOCUMENT_DID_CLOSE,
            notification_params=dict(
                textDocument=dict(uri=textDocument['uri'])
            )
        )

        # Technically this should be a request, but its not an issue since we don't need a response here
        self._send_notification(notification_method=Method.SHUTDOWN)

        # Make sure the shutdown worked, since now only exit notifications should
        # be accepted and all others should error
        response = self.server_conn.send_request(
            Method.COMPLETION.value, dict())
        InvalidRequestError = -32600
        self.assertEqual(response.get("error", {}).get(
            "code", -1), InvalidRequestError)

        self._send_notification(Method.EXIT)

        ret_code = server.wait(timeout=10)
        self.assertEqual(ret_code, 0)


def test_suite(test_completion, test_integration):
    suite = unittest.TestSuite()
    if test_completion:
        suite.addTests([
            ServerCompletionTests('test_basic'),
            ServerCompletionTests('test_probe_comps'),
            ServerCompletionTests('test_string'),
            ServerCompletionTests('test_body'),
            ServerCompletionTests('test_multiple_contexts'),
            ServerCompletionTests('test_no_completions')
        ])

    if test_integration:
        suite.addTest(ServerIntegrationTests('test_basic'))

    return suite


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.description = "Systemtap language server tests"
    parser.add_argument(
        "-v",
        "--verbose",
        help="increase verbosity of test",
        action="count",
        default=0,
    )
    parser.add_argument(
        "--stap-path",
        help="the path to the stap executable",
        type=str,
    )

    global CMD_ARGS
    CMD_ARGS = parser.parse_args()

    runner = unittest.TextTestRunner(failfast=True)
    ENV = dict(
        PATH=os.getenv("TEST_PATH"),
        TEST_C_FILE=os.getenv("TEST_C_FILE")
    )

    with mock.patch.dict(os.environ, ENV):
        res = runner.run(test_suite(
            test_completion=True, test_integration=True))
        sys.exit(0 if res.wasSuccessful() else 1)
