#!/usr/bin/env python3

import argparse
from html.parser import HTMLParser
import http.cookiejar
import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request


class CsrfParser(HTMLParser):
    def __init__(self):
        super().__init__()
        self.token = None

    def handle_starttag(self, tag, attrs):
        if tag != "input":
            return
        attributes = dict(attrs)
        if attributes.get("name") == "csrfmiddlewaretoken":
            self.token = attributes.get("value")


class SentryClient:
    def __init__(self, base_url, email, password):
        self.base_url = base_url.rstrip("/")
        self.email = email
        self.password = password
        self.cookies = http.cookiejar.CookieJar()
        self.opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(self.cookies))

    def request(self, path, data=None, json_data=None, method=None, headers=None, allowed=(200,)):
        url = path if path.startswith("http://") or path.startswith("https://") else f"{self.base_url}{path}"
        body = None
        request_headers = {"User-Agent": "sentry-qml-e2e"}
        if headers:
            request_headers.update(headers)
        if data is not None and json_data is not None:
            raise ValueError("Use either data or json_data, not both.")
        if data is not None:
            body = urllib.parse.urlencode(data, doseq=True).encode()
            request_headers.setdefault("Content-Type", "application/x-www-form-urlencoded")
        elif json_data is not None:
            body = json.dumps(json_data).encode()
            request_headers.setdefault("Content-Type", "application/json")

        request = urllib.request.Request(url, data=body, headers=request_headers, method=method)
        try:
            response = self.opener.open(request, timeout=30)
            status = response.getcode()
            text = response.read().decode("utf-8", errors="replace")
        except urllib.error.HTTPError as error:
            status = error.code
            text = error.read().decode("utf-8", errors="replace")

        if status not in allowed:
            raise RuntimeError(f"{method or 'GET'} {url} returned HTTP {status}:\n{text[:2000]}")
        return status, text

    def login(self):
        _, login_page = self.request("/auth/login/sentry/", headers={"Referer": self.base_url})
        parser = CsrfParser()
        parser.feed(login_page)
        if not parser.token:
            raise RuntimeError("Could not find login CSRF token.")

        self.request(
            "/auth/login/sentry/",
            data={
                "op": "login",
                "username": self.email,
                "password": self.password,
                "csrfmiddlewaretoken": parser.token,
            },
            headers={"Referer": f"{self.base_url}/auth/login/sentry/"},
        )

        if not self.cookie("sc"):
            raise RuntimeError("Login succeeded but the Sentry CSRF session cookie was not set.")

    def cookie(self, name):
        for cookie in self.cookies:
            if cookie.name == name:
                return cookie.value
        return None

    def configure_required_options(self):
        csrf = self.cookie("sc")
        self.request(
            "/api/0/internal/options/?query=is:required",
            method="PUT",
            data={
                "mail.use-tls": False,
                "mail.username": "",
                "mail.port": 25,
                "system.admin-email": self.email,
                "mail.password": "",
                "system.url-prefix": self.base_url,
                "auth.allow-registration": False,
                "beacon.anonymous": False,
            },
            headers={"Referer": self.base_url, "X-CSRFToken": csrf},
        )

    def json(self, path, allowed=(200,)):
        status, text = self.request(path, allowed=allowed)
        if status != 200:
            return status, None
        return status, json.loads(text)

    def create_auth_token(self):
        csrf = self.cookie("sc")
        _, text = self.request(
            "/api/0/api-tokens/",
            method="POST",
            json_data={
                "name": "sentry-qml-e2e",
                "scopes": ["event:read", "org:read", "project:read"],
            },
            headers={"Referer": self.base_url, "X-CSRFToken": csrf},
            allowed=(201,),
        )
        token = json.loads(text).get("token")
        if not token:
            raise RuntimeError("Created an auth token but the response did not include the token value.")
        return token


def poll(timeout, interval, description, callback):
    deadline = time.monotonic() + timeout
    last_error = None
    while time.monotonic() < deadline:
        try:
            result = callback()
            if result:
                return result
        except Exception as error:
            last_error = error
        time.sleep(interval)
    if last_error:
        raise RuntimeError(f"Timed out waiting for {description}: {last_error}") from last_error
    raise RuntimeError(f"Timed out waiting for {description}.")


def write_output(name, value, output_path, secret=False):
    if secret:
        print(f"::add-mask::{value}")
        print(f"{name}=***")
    else:
        print(f"{name}={value}")
    if output_path:
        with open(output_path, "a", encoding="utf-8") as output_file:
            output_file.write(f"{name}={value}\n")


def configure(args):
    client = SentryClient(args.base_url, args.email, args.password)
    client.login()
    client.configure_required_options()

    def fetch_dsn():
        _, keys = client.json("/api/0/projects/sentry/internal/keys/")
        if isinstance(keys, list) and keys:
            return keys[0].get("dsn", {}).get("public")
        return None

    dsn = poll(args.timeout, 2, "project DSN", fetch_dsn)
    auth_token = client.create_auth_token()
    write_output("dsn", dsn, args.github_output or os.environ.get("GITHUB_OUTPUT"))
    write_output("auth_token", auth_token, args.github_output or os.environ.get("GITHUB_OUTPUT"), secret=True)


def poll_event(args):
    with open(args.event_file, encoding="utf-8") as event_file:
        expected = json.load(event_file)

    event_id = expected["event_id"]
    event_ids = [event_id]
    if "-" in event_id:
        event_ids.append(event_id.replace("-", ""))

    client = SentryClient(args.base_url, args.email, args.password)
    client.login()

    def fetch_event():
        for candidate in event_ids:
            status, event = client.json(f"/api/0/projects/sentry/internal/events/{candidate}/", allowed=(200, 404))
            if status != 200:
                continue
            blob = json.dumps(event, sort_keys=True)
            if expected.get("message") not in blob:
                continue
            if expected.get("run_id") not in blob:
                continue
            return event
        return None

    event = poll(args.timeout, 2, f"event {event_id}", fetch_event)
    print(json.dumps({"event_id": event.get("eventID"), "title": event.get("title")}, sort_keys=True))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-url", default="http://127.0.0.1:9000")
    parser.add_argument("--email", default="test@example.com")
    parser.add_argument("--password", default="test123TEST")
    parser.add_argument("--timeout", type=int, default=180)

    subparsers = parser.add_subparsers(dest="command", required=True)

    configure_parser = subparsers.add_parser("configure")
    configure_parser.add_argument("--github-output")
    configure_parser.set_defaults(func=configure)

    poll_parser = subparsers.add_parser("poll-event")
    poll_parser.add_argument("--event-file", required=True)
    poll_parser.set_defaults(func=poll_event)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)
