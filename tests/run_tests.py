#!/usr/bin/env python3
"""Black-box myshell regressions. Python standard library + a C compiler only.

Default suite: core. Failing tests stay failing; no expected-failure masking.
Each case uses disposable files and a separate process group with a deadline.
"""
import argparse
import collections
import errno
import hashlib
import json
import os
from pathlib import Path
import platform
import pty
import select
import shlex
import signal
import subprocess
import sys
import tempfile
import time
import unittest

CASES = []
CONFIG = None
PROBE = None


def case(name, group="core", purpose=""):
    def register(fn):
        CASES.append((name, group, purpose or fn.__doc__ or name, fn))
        return fn
    return register


def stop_group(proc):
    try:
        os.killpg(proc.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    proc.wait()


class ShellCase(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="myshell-case-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.home = self.root / "home"
        self.home.mkdir()
        (self.root / "sub").mkdir()
        self.env = dict(os.environ, HOME=str(self.home), LC_ALL="C")
        self.transcripts = []

    def file(self, name, contents):
        p = self.root / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(contents.encode() if isinstance(contents, str) else contents)
        return p

    def run_shell(self, script, *, mode="stdin", env=None, timeout=None, args=None):
        payload = script.encode() if isinstance(script, str) else script
        command = [CONFIG.shell]
        if args is not None:
            command.extend(args)
            payload = b""
        elif mode == "batch":
            command.append(str(self.file(".input/batch", payload)))
            payload = b""
        proc = subprocess.Popen(command, cwd=self.root, env=env or self.env,
                                stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, start_new_session=True)
        try:
            out, err = proc.communicate(payload, timeout=timeout or CONFIG.timeout)
        except subprocess.TimeoutExpired:
            stop_group(proc)
            out, err = proc.communicate()
            self.transcripts.append(dict(script=script.decode(errors="replace") if isinstance(script, bytes) else script,
                                         mode=mode, timeout=True, stdout=out.decode(errors="replace")[:8000],
                                         stderr=err.decode(errors="replace")[:8000]))
            self.fail("shell or pipeline exceeded %.1fs (possible deadlock)" % (timeout or CONFIG.timeout))
        finally:
            # Also remove descendants if the shell exited while leaving children alive.
            stop_group(proc)
        record = dict(script=script.decode(errors="replace") if isinstance(script, bytes) else script,
                      mode=mode, returncode=proc.returncode, stdout=out.decode(errors="replace")[:8000],
                      stderr=err.decode(errors="replace")[:8000])
        self.transcripts.append(record)
        self.assertNotIn(b"AddressSanitizer", err, record["stderr"])
        self.assertNotIn(b"runtime error:", err, record["stderr"])
        self.assertGreaterEqual(proc.returncode, 0, "terminated by signal: " + repr(record))
        return out, err, proc.returncode

    def exact(self, script, expected, **kwargs):
        out, err, rc = self.run_shell(script, **kwargs)
        self.assertEqual(out, expected.encode() if isinstance(expected, str) else expected)
        self.assertEqual(err, b"")
        self.assertEqual(rc, 0)

    def recovered(self, prefix, expected=b"RECOVERED\n"):
        out, err, rc = self.run_shell(prefix + "\necho RECOVERED\n")
        self.assertTrue(err.strip() or b"parse error" in out or b"cd:" in out,
                        "invalid command must produce a diagnostic")
        # Accept diagnostics on either output stream, without accepting extra execution.
        useful = b"\n".join(line for line in out.splitlines()
                             if b"parse error" not in line and not line.startswith(b"cd:"))
        self.assertEqual(useful + b"\n", expected)
        self.assertEqual(rc, 0)


def exact_case(name, script, expected, group="core", purpose=""):
    @case(name, group, purpose)
    def check(t):
        t.exact(script(t) if callable(script) else script,
                expected(t) if callable(expected) else expected)


for name, script, expected in [
    ("empty_stdin", "", ""),
    ("blank_lines", "\n \n   \n", ""),
    ("echo_single", "echo hello\n", "hello\n"),
    ("echo_many_args", "echo alpha beta gamma\n", "alpha beta gamma\n"),
    ("repeated_spaces", "   echo   alpha    beta   \n", "alpha beta\n"),
    ("absolute_executable", "/bin/echo absolute\n", "absolute\n"),
    ("commands_continue_after_false", "/usr/bin/false\necho alive\n", "alive\n"),
    ("comments_full_line", "# comment\necho alive\n", "alive\n"),
    ("comments_after_command", "echo alive # ignored\n", "alive\n"),
    ("comments_pipe_ignored", "echo alive # | never_executed\n", "alive\n"),
    ("exit_stops_following_command", "echo before\nexit\necho after\n", "before\n"),
    ("one_pipe", "echo hello | /bin/cat\n", "hello\n"),
    ("three_stage_pipe", "echo hello | /bin/cat | /bin/cat\n", "hello\n"),
    ("adjacent_pipe_operator", "echo hello|/bin/cat\n", "hello\n"),
    ("pipeline_empty_producer", "/usr/bin/true | /bin/cat\n", ""),
]:
    exact_case(name, script, expected)

exact_case("pwd", "pwd\n", lambda t: str(t.root) + "\n")
exact_case("cd_relative", "cd sub\npwd\n", lambda t: str(t.root / "sub") + "\n")
exact_case("cd_parent", "cd sub\ncd ..\npwd\n", lambda t: str(t.root) + "\n")
exact_case("cd_absolute", lambda t: "cd %s\npwd\n" % t.home,
           lambda t: str(t.home) + "\n")
exact_case("cd_home", "cd\npwd\n", lambda t: str(t.home) + "\n")
exact_case("pwd_in_pipeline", "pwd | /bin/cat\n", lambda t: str(t.root) + "\n")
exact_case("args_exact", lambda t: PROBE + " args one two three\n", "[one]\n[two]\n[three]\n")
exact_case("argument_grows_past_50", lambda t: PROBE + " args " + "a" * 4097 + "\n",
           "[" + "a" * 4097 + "]\n")
exact_case("many_arguments", lambda t: PROBE + " args " + " ".join("v%d" % i for i in range(100)) + "\n",
           "".join("[v%d]\n" % i for i in range(100)))


@case("batch_mode")
def _(t):
    t.exact("echo batch\npwd\n", "batch\n" + str(t.root) + "\n", mode="batch")


@case("missing_batch_file")
def _(t):
    out, err, rc = t.run_shell("", args=[str(t.root / "absent")])
    t.assertEqual(out, b"")
    t.assertTrue(err.strip())
    t.assertNotEqual(rc, 0)


@case("relative_executable")
def _(t):
    os.symlink(PROBE, t.root / "local_probe")
    t.exact("./local_probe args local\n", "[local]\n")


@case("which_found")
def _(t):
    target = next(str(Path(d) / "echo") for d in ["/usr/local/bin", "/usr/bin", "/bin"]
                  if os.access(Path(d) / "echo", os.X_OK))
    t.exact("which echo\n", target + "\n")


@case("which_absolute")
def _(t):
    t.exact("which " + PROBE + "\n", PROBE + "\n")


@case("which_multiple")
def _(t):
    p = next(str(Path(d) / "echo") for d in ["/usr/local/bin", "/usr/bin", "/bin"]
             if os.access(Path(d) / "echo", os.X_OK))
    t.exact("which echo " + PROBE + "\n", p + "\n" + PROBE + "\n")


for name, prefix in [
    ("unknown_command_recovers", "zz_myshell_missing_command_701"),
    ("nonexistent_path_recovers", "./missing_program"),
    ("which_missing_argument_recovers", "which"),
    ("which_unknown_recovers", "which zz_myshell_missing_command_701"),
    ("pwd_extra_argument_recovers", "pwd extra"),
    ("cd_missing_directory_recovers", "cd missing_dir"),
    ("cd_too_many_arguments_recovers", "cd sub extra"),
    ("leading_pipe_recovers", "| echo BAD"),
    ("trailing_pipe_recovers", "echo BAD |"),
    ("double_pipe_recovers", "echo BAD || /bin/cat"),
    ("missing_input_target_recovers", "echo BAD <"),
    ("missing_output_target_recovers", "echo BAD >"),
    ("missing_append_target_recovers", "echo BAD >>"),
    ("consecutive_redirects_recovers", "echo BAD > > target"),
    ("unreadable_input_recovers", "/bin/cat < missing_input"),
    ("output_directory_recovers", "echo BAD > sub"),
]:
    @case(name)
    def _(t, prefix=prefix):
        t.recovered(prefix)


@case("non_executable_file_recovers")
def _(t):
    t.file("notexec", "plain text\n").chmod(0o600)
    t.recovered("./notexec")


@case("cd_error_preserves_directory")
def _(t):
    out, err, rc = t.run_shell("cd missing_dir\npwd\n")
    t.assertEqual(out, (str(t.root) + "\n").encode())
    t.assertTrue(err.strip())
    t.assertEqual(rc, 0)


@case("redirection_creates_file")
def _(t):
    t.exact("echo payload > out\n", "")
    t.assertEqual((t.root / "out").read_bytes(), b"payload\n")


@case("redirection_truncates_file")
def _(t):
    t.file("out", "old and much longer payload\n")
    t.exact("echo new > out\n", "")
    t.assertEqual((t.root / "out").read_bytes(), b"new\n")


@case("append_creates_file")
def _(t):
    t.exact("echo first >> out\n", "")
    t.assertEqual((t.root / "out").read_bytes(), b"first\n")


@case("append_preserves_file")
def _(t):
    t.file("out", "first\n")
    t.exact("echo second >> out\n", "")
    t.assertEqual((t.root / "out").read_bytes(), b"first\nsecond\n")


@case("input_redirection")
def _(t):
    t.file("in", "alpha\nbeta\n")
    t.exact(PROBE + " copy < in\n", "alpha\nbeta\n")


@case("input_and_output_redirection")
def _(t):
    t.file("in", "alpha\nbeta\n")
    t.exact(PROBE + " copy < in > out\n", "")
    t.assertEqual((t.root / "out").read_bytes(), b"alpha\nbeta\n")


@case("redirection_without_spaces")
def _(t):
    t.file("in", "data\n")
    t.exact("/bin/cat<in>out\n", "")
    t.assertEqual((t.root / "out").read_bytes(), b"data\n")


@case("append_without_spaces")
def _(t):
    t.file("out", "old\n")
    t.exact("echo new>>out\n", "")
    t.assertEqual((t.root / "out").read_bytes(), b"old\nnew\n")


@case("stdout_restored_after_redirection")
def _(t):
    t.exact("echo first > out\necho visible\n", "visible\n")
    t.assertEqual((t.root / "out").read_bytes(), b"first\n")


@case("stderr_not_redirected_by_stdout_operator")
def _(t):
    out, err, rc = t.run_shell(PROBE + " out-err > out\n")
    t.assertEqual(out, b"")
    t.assertEqual(err, b"ERR\n")
    t.assertEqual(rc, 0)
    t.assertEqual((t.root / "out").read_bytes(), b"OUT\n")


@case("builtin_pwd_redirection")
def _(t):
    t.exact("pwd > out\n", "")
    t.assertEqual((t.root / "out").read_bytes(), (str(t.root) + "\n").encode())


@case("pipeline_with_redirection")
def _(t):
    t.file("in", "pipe data\n")
    t.exact(PROBE + " copy < in | " + PROBE + " copy > out\n", "")
    t.assertEqual((t.root / "out").read_bytes(), b"pipe data\n")


@case("pipe_stdout_redirect_overrides_pipe")
def _(t):
    t.exact("echo file > out | /bin/cat\n", "")
    t.assertEqual((t.root / "out").read_bytes(), b"file\n")


@case("pipeline_input_redirection_overrides_pipe")
def _(t):
    t.file("in", "file wins\n")
    t.exact("echo pipe | /bin/cat < in\n", "file wins\n")


@case("commands_do_not_share_pipeline_arguments")
def _(t):
    t.exact("echo one | /bin/cat\necho two\n", "one\ntwo\n")


@case("pipeline_stages_have_distinct_arguments")
def _(t):
    t.exact("echo wrong | " + PROBE + " args right\n", "[right]\n")


@case("nonfinal_cd_isolated_from_parent")
def _(t):
    t.exact("cd sub | /bin/cat\npwd\n", str(t.root) + "\n")


@case("final_cd_isolated_from_parent")
def _(t):
    t.exact("echo ignored | cd sub\npwd\n", str(t.root) + "\n")


def glob_fixture(t):
    for name in ["alpha.txt", "beta.txt", "alpine.log", ".hidden.txt", "a.txt", "x.txt"]:
        t.file("glob/" + name, name + "\n")


for name, pattern, expected in [
    ("glob_suffix", "*.txt", ["a.txt", "alpha.txt", "beta.txt", "x.txt"]),
    ("glob_prefix", "al*", ["alpha.txt", "alpine.log"]),
    ("glob_middle", "a*l*.txt", ["alpha.txt"]),
    ("glob_multiple_stars", "*l*.txt", ["alpha.txt"]),
    ("glob_star_matches_zero_chars", "a*.txt", ["a.txt", "alpha.txt"]),
    ("glob_unmatched_literal", "z*.none", ["z*.none"]),
]:
    @case(name)
    def _(t, pattern=pattern, expected=expected):
        glob_fixture(t)
        out, err, rc = t.run_shell("cd glob\n" + PROBE + " args " + pattern + "\n")
        t.assertEqual(sorted(out.decode().splitlines()), sorted("[" + s + "]" for s in expected))
        t.assertEqual(err, b"")
        t.assertEqual(rc, 0)


@case("glob_relative_directory")
def _(t):
    glob_fixture(t)
    out, err, rc = t.run_shell(PROBE + " args glob/al*.txt\n")
    t.assertEqual(out, b"[glob/alpha.txt]\n")
    t.assertEqual(err, b"")
    t.assertEqual(rc, 0)


@case("glob_absolute_directory")
def _(t):
    glob_fixture(t)
    t.exact(PROBE + " args " + str(t.root / "glob/al*.txt") + "\n",
            "[" + str(t.root / "glob/alpha.txt") + "]\n")


@case("glob_redirection_single_match")
def _(t):
    t.file("one.input", "expanded\n")
    t.exact("/bin/cat < *.input\n", "expanded\n")


@case("parser_read_chunk_boundaries")
def _(t):
    sizes = [9, 10, 11, 19, 20, 21]
    lines = ["echo " + "a" * (n - 6) + "\n" for n in sizes]
    t.exact("".join(lines), "".join("a" * (n - 6) + "\n" for n in sizes))


@case("arguments_shrink_between_commands")
def _(t):
    t.exact(PROBE + " args a b c d e\n" + PROBE + " args z\n",
            "[a]\n[b]\n[c]\n[d]\n[e]\n[z]\n")


@case("nonzero_external_status_does_not_stop_shell")
def _(t):
    t.exact(PROBE + " status 7\necho alive\n", "alive\n")


@case("output_missing_parent_recovers")
def _(t):
    t.recovered("echo BAD > no_such_parent/out")
    t.assertFalse((t.root / "no_such_parent").exists())


@case("cd_to_regular_file_recovers")
def _(t):
    t.file("regular", "data\n")
    t.recovered("cd regular")


# These expectations are common shell semantics. Confirm them against the course handout.
exact_case("eof_without_newline", "echo final", "final\n", "review",
           "Execute a final nonempty line terminated by EOF.")
exact_case("tab_separates_arguments", "echo\talpha\tbeta\n", "alpha beta\n", "review")
exact_case("crlf_batch_lines", b"echo windows\r\n", "windows\n", "review")


@case("arguments_after_redirection", "review")
def _(t):
    t.exact("echo first > out second\n", "")
    t.assertEqual((t.root / "out").read_bytes(), b"first second\n")


@case("cd_redirection_changes_parent_directory", "review")
def _(t):
    t.exact("cd sub > out\npwd\n", str(t.root / "sub") + "\n")
    t.assertTrue((t.root / "out").is_file())


@case("exit_in_pipeline_preserves_parent", "review")
def _(t):
    t.exact("echo before | exit\necho alive\n", "alive\n")


@case("missing_home_is_handled", "review")
def _(t):
    env = dict(t.env)
    del env["HOME"]
    out, err, rc = t.run_shell("echo alive\n", env=env)
    t.assertEqual(out, b"alive\n")
    t.assertEqual(err, b"")
    t.assertEqual(rc, 0)


@case("glob_absolute_unmatched_preserves_token", "review")
def _(t):
    glob_fixture(t)
    token = str(t.root / "glob/absent*.none")
    t.exact(PROBE + " args " + token + "\n", "[" + token + "]\n")


@case("pipeline_large_payload", "stress")
def _(t):
    t.exact(PROBE + " emit 1048576 | " + PROBE + " copy\n", b"x" * 1048576)


@case("pipeline_ten_stages", "stress")
def _(t):
    t.exact("echo flow" + " | /bin/cat" * 9 + "\n", "flow\n")


@case("repeated_commands", "stress")
def _(t):
    t.exact("echo repeat\n" * 150, "repeat\n" * 150, timeout=max(10, CONFIG.timeout))


@case("repeated_pipelines", "stress")
def _(t):
    t.exact("echo repeat | /bin/cat\n" * 80, "repeat\n" * 80, timeout=max(10, CONFIG.timeout))


@case("fd_growth_after_pipelines", "stress")
def _(t):
    cmd = PROBE + " fds\n"
    out, err, rc = t.run_shell(cmd + "/usr/bin/true | /bin/cat\n" * 40 + cmd,
                               timeout=max(10, CONFIG.timeout))
    counts = [int(s) for s in out.splitlines()]
    t.assertEqual(len(counts), 2)
    t.assertLessEqual(counts[1], counts[0], "pipe descriptors leaked into subsequent children")
    t.assertEqual(err, b"")
    t.assertEqual(rc, 0)


@case("fd_growth_in_long_pipeline", "stress")
def _(t):
    script = PROBE + " fds\n/usr/bin/true" + " | /bin/cat" * 24 + " | " + PROBE + " fds\n"
    out, err, rc = t.run_shell(script)
    counts = [int(s) for s in out.splitlines()]
    t.assertEqual(len(counts), 2)
    t.assertLessEqual(counts[1], counts[0], "later pipeline stages inherit older read ends")
    t.assertEqual(err, b"")
    t.assertEqual(rc, 0)


@case("memory_empty_input", "memory")
def _(t):
    t.exact("", "")


@case("memory_one_command", "memory")
def _(t):
    t.exact("echo alive\n", "alive\n")


@case("memory_wildcard_many_matches", "memory")
def _(t):
    for i in range(30): t.file("glob/f%02d.txt" % i, "")
    out, err, rc = t.run_shell("cd glob\n" + PROBE + " args *.txt\n")
    t.assertEqual(sorted(out.decode().splitlines()), ["[f%02d.txt]" % i for i in range(30)])
    t.assertEqual(err, b"")
    t.assertEqual(rc, 0)


def interactive_session(t, command, eof=False):
    master, slave = pty.openpty()
    proc = subprocess.Popen([CONFIG.shell], stdin=slave, stdout=slave, stderr=slave,
                            cwd=t.root, env=t.env, start_new_session=True)
    os.close(slave)
    data = bytearray()
    deadline = time.monotonic() + max(5, CONFIG.timeout)
    def until(needle):
        start = len(data)
        while needle not in data[start:]:
            remaining = deadline - time.monotonic()
            if remaining <= 0: t.fail("interactive deadline; transcript=" + repr(bytes(data)))
            if not select.select([master], [], [], remaining)[0]: continue
            try: block = os.read(master, 65536)
            except OSError as e:
                if e.errno == errno.EIO: break
                raise
            if not block: break
            data.extend(block)
        t.assertIn(needle, bytes(data[start:]), repr(bytes(data)))
    try:
        until(b"$ ")
        if command:
            os.write(master, command.encode() + b"\n")
            until(b"$ ")
        os.write(master, b"\x04" if eof else b"exit\n")
        until(b"Exiting myshell")
        proc.wait(timeout=max(.1, deadline - time.monotonic()))
        t.assertEqual(proc.returncode, 0)
        t.assertIn(b"Welcome to my shell!", data)
        t.assertNotIn(b"AddressSanitizer", data)
        t.assertNotIn(b"runtime error:", data)
        return bytes(data).replace(b"\r\n", b"\n")
    finally:
        stop_group(proc)
        os.close(master)
        t.transcripts.append(dict(mode="pty", command=command,
                                  stdout=bytes(data).decode(errors="replace")[:8000]))


@case("interactive_command_and_exit", "interactive")
def _(t):
    transcript = interactive_session(t, "echo INTERACTIVE_OK")
    t.assertIn(b"\nINTERACTIVE_OK\n", transcript)


@case("interactive_ctrl_d", "interactive")
def _(t):
    interactive_session(t, "", eof=True)


@case("interactive_cd_updates_prompt", "interactive")
def _(t):
    transcript = interactive_session(t, "cd sub")
    t.assertIn((str(t.root / "sub") + "$ ").encode(), transcript)


class JsonResult(unittest.TextTestResult):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.records = []

    def startTest(self, test):
        self.started = time.monotonic()
        super().startTest(test)

    def addSuccess(self, test):
        super().addSuccess(test)
        self.record(test, "pass")

    def addFailure(self, test, err):
        super().addFailure(test, err)
        self.record(test, "fail", self._exc_info_to_string(err, test))

    def addError(self, test, err):
        super().addError(test, err)
        self.record(test, "error", self._exc_info_to_string(err, test))

    def record(self, test, status, detail=""):
        name, group, purpose = test.metadata
        self.records.append(dict(name=name, group=group, purpose=purpose, status=status,
                                 duration_seconds=round(time.monotonic() - self.started, 4),
                                 detail=detail[:20000], transcripts=getattr(test, "transcripts", [])))


def main():
    global CONFIG, PROBE
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--shell", default="./myshell", help="already compiled myshell executable")
    parser.add_argument("--suite", choices=["core", "review", "stress", "interactive", "memory", "all"], default="core")
    parser.add_argument("--filter", default="", help="substring of a test name")
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--json", type=Path, help="write machine-readable test results")
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"), help="compiler command, shell quoting accepted")
    parser.add_argument("--list", action="store_true")
    CONFIG = parser.parse_args()
    selected = [(n, g, p, fn) for n, g, p, fn in CASES
                if (CONFIG.suite == "all" or CONFIG.suite == g) and CONFIG.filter in n]
    if CONFIG.list:
        for name, group, purpose, _ in selected: print("%-12s %-48s %s" % (group, name, purpose.strip()))
        print("%d tests" % len(selected))
        return 0
    if not selected: parser.error("no tests selected")
    if CONFIG.timeout <= 0: parser.error("--timeout must be positive")
    CONFIG.shell = str(Path(CONFIG.shell).resolve())
    if not os.path.isfile(CONFIG.shell) or not os.access(CONFIG.shell, os.X_OK):
        parser.error("--shell must name an executable; compile myshell from source first")
    with tempfile.TemporaryDirectory(prefix="myshell-tools-") as tools:
        PROBE = str(Path(tools).resolve() / "probe")
        compiler = shlex.split(CONFIG.cc) + shlex.split(os.environ.get("CFLAGS", ""))
        build = subprocess.run(compiler + ["-Wall", "-Wextra", "-std=c99",
                                         str(Path(__file__).with_name("probe.c")), "-o", PROBE],
                               capture_output=True, text=True, timeout=30)
        if build.returncode: parser.error("cannot compile test helper:\n" + build.stderr)
        suite = unittest.TestSuite()
        for name, group, purpose, fn in selected:
            method = "test_" + name
            setattr(ShellCase, method, fn)
            test = ShellCase(method)
            test.metadata = (name, group, purpose.strip())
            suite.addTest(test)
        result = unittest.TextTestRunner(verbosity=2, resultclass=JsonResult).run(suite)
        if CONFIG.json:
            CONFIG.json.parent.mkdir(parents=True, exist_ok=True)
            summary = collections.Counter(r["status"] for r in result.records)
            payload = dict(schema_version=1, platform=platform.platform(), python=sys.version,
                           shell=CONFIG.shell, shell_sha256=hashlib.sha256(Path(CONFIG.shell).read_bytes()).hexdigest(),
                           suite=CONFIG.suite, summary=dict(summary), tests=result.records)
            CONFIG.json.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n")
        return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
