#!/usr/bin/env python3
#
# Minimal HTTP client for exporter.exp.
#
# Prints the HTTP status code on the first line, followed by the response
# body.  Prints status 0 (and an error message) if the request could not be
# completed.  Proxy settings are ignored, since the exporter is local, and
# the whole operation is bounded by a hard timeout.

import signal
import sys
import urllib.error
import urllib.request


def _alarm(signum, frame):
    raise TimeoutError("request timed out")


def main():
    if len(sys.argv) < 2:
        sys.stderr.write("usage: %s URL [TIMEOUT]\n" % (sys.argv[0],))
        return 2

    url = sys.argv[1]
    timeout = float(sys.argv[2]) if len(sys.argv) > 2 else 10.0

    signal.signal(signal.SIGALRM, _alarm)
    signal.alarm(int(timeout) + 5)

    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    try:
        with opener.open(url, timeout=timeout) as response:
            code = response.getcode()
            headers = response.headers
            body = response.read()
    except urllib.error.HTTPError as e:
        code = e.code
        headers = e.headers
        body = e.read()
    except Exception as e:
        sys.stdout.write("0\n%s\n" % (str(e),))
        return 0

    # The exporter must always frame the body and close the connection.
    # Without this, a keep-alive client can hang waiting for EOF.
    content_length = headers.get("Content-Length")
    if content_length != str(len(body)):
        sys.stdout.write("0\nbad Content-Length %r for %d byte body\n"
                         % (content_length, len(body)))
        return 0
    if headers.get("Connection", "").lower() != "close":
        sys.stdout.write("0\nmissing Connection: close\n")
        return 0

    sys.stdout.write("%d\n" % (code,))
    sys.stdout.write(body.decode("utf-8", "replace"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
