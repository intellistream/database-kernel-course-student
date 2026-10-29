#!/usr/bin/env python3
"""Public lab runner. Registrations and executed tests are separate evidence."""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict
from contextlib import contextmanager
from dataclasses import asdict, dataclass
import fcntl
import json
import math
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[1]
QUESTION_ID = re.compile(r"S(0[1-9]|10)\.Q(0[1-9]|[1-9][0-9])\Z")
TEST_NAME = re.compile(r"[A-Za-z0-9_/]+\.[A-Za-z0-9_/]+\Z")


class RunnerError(Exception):
    """Invalid input or missing evidence; never an accepted test result."""


def require(condition, message):
    if not condition:
        raise RunnerError(message)


def read_json(path):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise RunnerError(f"cannot read JSON {path}: {exc}") from exc


def unique_strings(value, label, *, nonempty=True):
    require(isinstance(value, list), f"{label} must be a list")
    require(all(isinstance(v, str) and v for v in value), f"invalid {label}")
    require(len(value) == len(set(value)), f"duplicate {label}")
    require(bool(value) or not nonempty, f"empty {label}")
    return value


def keyed(items, label):
    require(isinstance(items, list), f"{label} must be a list")
    result = {}
    for item in items:
        require(isinstance(item, dict), f"invalid {label} entry")
        key = item.get("id")
        require(isinstance(key, str) and key, f"missing {label} id")
        require(key not in result, f"duplicate {label} id: {key}")
        result[key] = item
    return result


def relative_path(value, label):
    require(isinstance(value, str) and value, f"missing {label}")
    path = Path(value)
    require(not path.is_absolute() and ".." not in path.parts and path != Path("."),
            f"{label} must be a relative path inside the build: {value}")
    return value


@dataclass(frozen=True, order=True)
class TestId:
    binary: str
    name: str


def test_ids(value, label):
    require(isinstance(value, list) and value, f"{label}: missing nonempty real test_ids registration")
    result = []
    for entry in value:
        require(isinstance(entry, dict), f"{label}: expected binary/name object")
        binary = relative_path(entry.get("binary"), f"{label}.binary")
        name = entry.get("name")
        require(isinstance(name, str) and TEST_NAME.fullmatch(name),
                f"{label}: expected exact GoogleTest name, not a filter: {name}")
        require(not any(part.startswith("DISABLED_") for part in re.split(r"[./]", name)),
                f"{label}: disabled test cannot be accepted: {name}")
        result.append(TestId(binary, name))
    require(len(set(result)) == len(result), f"{label}: duplicate test registration")
    return result


class Catalog:
    def __init__(self, manifest, design=None):
        require(isinstance(manifest, dict) and manifest.get("schema_version") == 2,
                "manifest requires schema_version 2")
        design = design or {}
        self.manifest = manifest
        self.questions = keyed(manifest.get("questions"), "question")
        require(bool(self.questions), "empty question bank")
        # Read only public catalog fields. Never include baseline metadata in reports.
        self.capabilities = keyed(manifest.get("capabilities", design.get("capabilities", [])), "capability")
        self.profiles = keyed(manifest.get("profiles", design.get("profiles", [])), "profile")
        self.integration_design = manifest.get("integration_test_design", design.get("integration_test_design", []))
        self.stages = manifest.get("stages", design.get("stages", []))
        design_questions = keyed(design.get("questions", []), "design question")
        self.weights = {qid: question.get("effort_weight", design_questions.get(qid, {}).get("effort_weight", 1))
                        for qid, question in self.questions.items()}
        self.validate()

    @classmethod
    def load(cls, root):
        manifest = read_json(root / "labs/manifest.json")
        needed = ("capabilities", "stages", "integration_test_design")
        design_path = root / "docs/course/question-bank.json"
        design = read_json(design_path) if any(k not in manifest for k in needed) and design_path.exists() else {}
        return cls(manifest, design)

    def check_questions(self, value, label="selected questions", *, nonempty=True):
        result = unique_strings(value, label, nonempty=nonempty)
        unknown = set(result) - self.questions.keys()
        require(not unknown, f"unknown question: {', '.join(sorted(unknown))}")
        return sorted(result)

    def requirements(self, capability):
        value = self.capabilities[capability].get("requirements")
        require(isinstance(value, dict) and len(value) == 1 and next(iter(value)) in ("all", "any"),
                f"{capability}: requirements must contain exactly one all/any list")
        mode, refs = next(iter(value.items()))
        unique_strings(refs, f"{capability} requirements")
        for ref in refs:
            require(ref in self.questions or ref in self.capabilities, f"{capability}: unknown capability/question {ref}")
        return mode, refs

    def validate_choices(self, choices):
        require(isinstance(choices, dict), "choices must be an object")
        for capability, choice in choices.items():
            require(capability in self.capabilities, f"unknown choice capability: {capability}")
            mode, options = self.requirements(capability)
            require(mode == "any", f"{capability} is not an any capability")
            require(isinstance(choice, str) and choice in options,
                    f"{capability}: invalid choice {choice}; choose from {', '.join(options)}")

    def validate(self):
        region_ids, markers = set(), set()
        for qid, question in self.questions.items():
            match = QUESTION_ID.fullmatch(qid)
            require(match is not None, f"invalid question id: {qid}")
            require(question.get("stage") == int(match.group(1)), f"{qid}: inconsistent stage")
            weight = self.weights[qid]
            require(isinstance(weight, (int, float)) and not isinstance(weight, bool)
                    and math.isfinite(weight) and weight > 0, f"{qid}: invalid effort weight")
            regions = question.get("regions", [])
            require(isinstance(regions, list), f"{qid}: regions must be a list")
            for region in regions:
                require(isinstance(region, dict), f"{qid}: invalid region")
                rid = region.get("id")
                require(isinstance(rid, str) and rid.startswith(qid + ":")
                        and region.get("owner") == qid, f"{qid}: invalid region owner/id")
                require(rid not in region_ids, f"duplicate region: {rid}")
                region_ids.add(rid)
                marker = region.get("marker")
                if marker is not None:
                    require(isinstance(marker, str) and marker and marker not in markers, f"duplicate/invalid region marker: {marker}")
                    markers.add(marker)
            unit = question.get("unit", {})
            require(isinstance(unit, dict), f"{qid}: unit must be an object")
            require(unit.get("requires_completed_questions", []) == [],
                    f"{qid}: independent unit tests cannot require other completed questions")
            if "test_ids" in unit:
                test_ids(unit["test_ids"], f"{qid}.unit")
        require(not (self.questions.keys() & self.capabilities.keys()), "question and capability IDs overlap")
        done, visiting = set(), []

        def visit(capability):
            if capability in done:
                return
            require(capability not in visiting, "capability cycle: " + " -> ".join(visiting + [capability]))
            visiting.append(capability)
            _, refs = self.requirements(capability)
            for ref in refs:
                if ref in self.capabilities:
                    visit(ref)
            visiting.pop()
            done.add(capability)

        for capability in self.capabilities:
            visit(capability)
        for pid, profile in self.profiles.items():
            mode = profile.get("mode")
            require(mode in ("unit", "integration"), f"{pid}: invalid profile mode")
            if mode == "unit":
                self.check_questions(profile.get("selection"), pid)
            else:
                for ref in unique_strings(profile.get("requires"), f"{pid} requires"):
                    require(ref in self.capabilities or ref in self.questions, f"{pid}: unknown requirement {ref}")
            self.validate_choices(profile.get("default_choices", {}))
        for group in self.integration_design:
            require(isinstance(group, dict), "invalid integration design")
            for ref in unique_strings(group.get("requires"), "integration design requires"):
                require(ref in self.capabilities or ref in self.questions, f"unknown integration requirement: {ref}")

    def expand(self, profile_id, overrides=None):
        require(profile_id in self.profiles, f"unknown profile: {profile_id}")
        profile = self.profiles[profile_id]
        choices = dict(profile.get("default_choices", {}))
        choices.update(overrides or {})
        self.validate_choices(choices)
        selected, visited, used = set(), set(), {}

        def visit(ref):
            if ref in self.questions:
                selected.add(ref)
                return
            if ref in visited:
                return
            visited.add(ref)
            mode, refs = self.requirements(ref)
            if mode == "any":
                require(ref in choices, f"{ref}: choice required ({', '.join(refs)})")
                used[ref] = choices[ref]
                visit(choices[ref])
            else:
                for child in refs:
                    visit(child)

        if profile["mode"] == "unit":
            selected.update(profile["selection"])
        else:
            for ref in profile["requires"]:
                visit(ref)
        unused = choices.keys() - used.keys()
        require(not unused, f"choice does not apply to {profile_id}: {', '.join(sorted(unused))}")
        if profile_id == "db-reference":
            require(selected == self.questions.keys(), "db-reference must require every question")
        return sorted(selected), sorted(visited), used

    def unit_tests(self, qid):
        return test_ids(self.questions[qid].get("unit", {}).get("test_ids"), qid + ".unit")

    def integration(self, profile_id):
        integration = self.manifest.get("integration", {})
        require(isinstance(integration, dict), "integration must be an object")
        profiles = integration.get("profiles", {})
        require(isinstance(profiles, dict), "integration.profiles must be an object")
        entry = profiles.get(profile_id)
        require(isinstance(entry, dict), f"{profile_id}: missing real Database/Session integration registration; product capabilities unverified")
        require(entry.get("kind") == "database_session", f"{profile_id}: integration kind must be database_session")
        capability_tests = test_ids(entry.get("capability_test_ids"), profile_id + ".capability_test_ids (product capabilities unverified)")
        tests = test_ids(entry.get("test_ids"), profile_id + ".test_ids")
        require(not (set(tests) & set(capability_tests)), f"{profile_id}: duplicate capability/SQL test registration")
        if profile_id == "db-reference":
            require(entry.get("full_regression") is True, "db-reference requires full_regression=true")
        return capability_tests, tests


def parse_choices(raw):
    result = {}
    for value in raw:
        require(value.count("=") == 1, f"expected --choice capability=question, got {value}")
        capability, choice = value.split("=")
        require(capability and choice and capability not in result, f"empty or duplicate choice: {value}")
        result[capability] = choice
    return result


def read_selection(path, catalog):
    data = read_json(path)
    require(isinstance(data, dict) and data.get("schema_version") == 2, "selection requires schema_version 2")
    selected = catalog.check_questions(data.get("selected_questions"))
    weights = data.get("weights")
    require(isinstance(weights, dict) and weights.keys() == set(selected), "weights must name each selected question exactly once")
    for qid, weight in weights.items():
        require(isinstance(weight, (int, float)) and not isinstance(weight, bool)
                and math.isfinite(weight) and weight > 0, f"invalid weight: {qid}")
    profiles = unique_strings(data.get("requested_profiles", []), "requested_profiles", nonempty=False)
    for profile in profiles:
        require(profile in catalog.profiles and catalog.profiles[profile]["mode"] == "integration",
                f"unknown database profile: {profile}")
    choices = data.get("choices", {})
    catalog.validate_choices(choices)
    return selected, weights, profiles, choices


def plan(catalog, profile_id, choices, selected=None):
    required, capabilities, used = catalog.expand(profile_id, choices)
    selected = required if selected is None else selected
    missing = sorted(set(required) - set(selected))
    registration_errors = []
    registrations = []
    for qid in required:
        try:
            catalog.unit_tests(qid)
        except RunnerError as exc:
            registration_errors.append(str(exc))
    if catalog.profiles[profile_id]["mode"] == "integration":
        try:
            groups = catalog.integration(profile_id)
            registrations = [asdict(test) for group in groups for test in group]
        except RunnerError as exc:
            registration_errors.append(str(exc))
    return {
        "profile": profile_id, "mode": catalog.profiles[profile_id]["mode"],
        "required_questions": required, "selected_questions": sorted(selected),
        "missing_questions": missing, "capabilities": capabilities, "choices": used,
        "status": "missing_questions" if missing else "not_run",
        "product_capabilities": "unverified", "registration_errors": registration_errors,
        "integration_test_ids": registrations,
        "integration_design": [group["id"] for group in catalog.integration_design
                               if set(group["requires"]) <= (set(capabilities) | set(required))],
    }


def jobs():
    raw = os.getenv("LABS_JOBS")
    try:
        value = int(raw) if raw is not None else min(16, os.cpu_count() or 1)
    except ValueError as exc:
        raise RunnerError("LABS_JOBS must be an integer from 1 to 16") from exc
    require(1 <= value <= 16, "LABS_JOBS must be an integer from 1 to 16")
    return value


def clean_environment(extra=None):
    controlled = {"TESTBRIDGE_TEST_ONLY", "LABS_QUESTION", "LABS_PROFILE", "LABS_CAPABILITIES",
                  "LABS_CHOICES", "LABS_SELECTED_QUESTIONS"}
    env = {key: value for key, value in os.environ.items()
           if not key.startswith("GTEST_") and key not in controlled}
    env.update(extra or {})
    return env


@dataclass
class ProcessResult:
    returncode: int
    stdout: str
    stderr: str
    timed_out: bool = False


def run_process(command, cwd, timeout, extra_env=None):
    try:
        process = subprocess.Popen(command, cwd=cwd, env=clean_environment(extra_env),
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   text=True, encoding="utf-8", errors="replace", start_new_session=True)
    except OSError as exc:
        raise RunnerError(f"cannot start {command[0]}: {exc}") from exc
    timed_out = False
    try:
        stdout, stderr = process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        timed_out = True
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        stdout, stderr = process.communicate()
    return ProcessResult(process.returncode, stdout, stderr, timed_out)


@contextmanager
def build_lock(build):
    build.mkdir(parents=True, exist_ok=True)
    with (build / "build.lock").open("a", encoding="utf-8") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        try:
            yield
        finally:
            fcntl.flock(lock, fcntl.LOCK_UN)


def build_project(root, build):
    require(build != root, "in-source builds are not supported")
    commands = [
        ["cmake", "-S", str(root), "-B", str(build), "-G", "Ninja",
         "-DCMAKE_BUILD_TYPE=Release", "-DBUILD_TESTING=ON", "-DDB_WARNINGS_AS_ERRORS=ON"],
        ["cmake", "--build", str(build), "--parallel", str(jobs())],
    ]
    # BUILD_TESTING keeps the public/private header OBJECT libraries in ALL.
    for command in commands:
        print("+ " + " ".join(command), file=sys.stderr)
        completed = subprocess.run(command, cwd=root, stdout=sys.stderr, stderr=sys.stderr, check=False)
        if completed.returncode:
            return {"status": "failed", "returncode": completed.returncode, "command": command}
    return {"status": "passed", "scope": "configure, full build/link, public/private header checks"}


def parse_gtest_listing(output):
    suite = None
    names = set()
    for raw in output.splitlines():
        line = raw.split("#", 1)[0].rstrip()
        if not line:
            continue
        if not line[0].isspace():
            suite = line if line.endswith(".") and " " not in line else None
        elif suite:
            name = suite + line.strip()
            require(TEST_NAME.fullmatch(name) is not None, f"invalid GoogleTest listing entry: {name}")
            require(name not in names, f"duplicate GoogleTest listing entry: {name}")
            names.add(name)
    require(bool(names), "GoogleTest enumeration found zero real cases")
    return names


class Executor:
    def __init__(self, build, timeout):
        self.build = build.resolve()
        self.timeout = timeout
        self.discovered = {}

    def binary(self, binary):
        path = (self.build / relative_path(binary, "binary")).resolve()
        require(path.is_relative_to(self.build), f"binary escapes build directory: {binary}")
        require(path.is_file() and os.access(path, os.X_OK), f"test binary is missing/not executable: {binary}")
        return path

    def discover(self, binary):
        if binary not in self.discovered:
            result = run_process([str(self.binary(binary)), "--gtest_list_tests"], self.build, min(self.timeout, 60))
            require(result.returncode == 0 and not result.timed_out,
                    f"GoogleTest enumeration failed: {binary}; exit {result.returncode}; {result.stderr[-2000:]}")
            self.discovered[binary] = parse_gtest_listing(result.stdout)
        return self.discovered[binary]

    def preflight(self, groups):
        for tests in groups:
            for test in tests:
                require(test.name in self.discover(test.binary),
                        f"unknown test/zero-match filter: {test.binary}::{test.name}")

    def run_group(self, tests, extra_env=None):
        require(bool(tests), "cannot run an empty test group")
        self.preflight([tests])
        records = {test: {**asdict(test), "status": "not_run"} for test in tests}
        processes = []
        grouped = defaultdict(list)
        for test in tests:
            grouped[test.binary].append(test)
        for binary, group in grouped.items():
            with tempfile.TemporaryDirectory(prefix="labs-results-", dir=self.build) as temp:
                output = Path(temp) / "gtest.xml"
                command = [str(self.binary(binary)), "--gtest_filter=" + ":".join(t.name for t in group),
                           "--gtest_output=xml:" + str(output), "--gtest_color=no", "--gtest_repeat=1",
                           "--gtest_shuffle=0", "--gtest_also_run_disabled_tests=0"]
                result = run_process(command, self.build, self.timeout, extra_env)
                process = {"binary": binary, "returncode": result.returncode, "timed_out": result.timed_out}
                try:
                    outcomes = parse_gtest_results(output, {test.name for test in group})
                    for test in group:
                        records[test]["status"] = outcomes[test.name]
                except RunnerError as exc:
                    process["report_error"] = str(exc)
                if result.returncode or result.timed_out or "report_error" in process:
                    process["output"] = (result.stdout + result.stderr)[-12000:]
                processes.append(process)
        cases = list(records.values())
        passed = all(case["status"] == "passed" for case in cases) and all(
            item["returncode"] == 0 and not item["timed_out"] and "report_error" not in item for item in processes)
        return {"status": "passed" if passed else "failed", "discovered_cases": len(tests),
                "cases": cases, "processes": processes, "case_counts": case_counts(cases)}


def parse_gtest_results(path, expected):
    try:
        document = ET.parse(path)
    except (OSError, ET.ParseError) as exc:
        raise RunnerError(f"missing or malformed GoogleTest XML: {exc}") from exc
    result = {}
    for case in document.iter("testcase"):
        name = case.get("classname", "") + "." + case.get("name", "")
        require(name in expected and name not in result, f"unexpected/duplicate GoogleTest result: {name}")
        if case.find("skipped") is not None or case.get("result") == "skipped":
            result[name] = "skipped"
        elif case.get("status") != "run" or case.get("result", "completed") != "completed":
            result[name] = "not_run"
        elif case.find("failure") is not None or case.find("error") is not None:
            result[name] = "failed"
        else:
            result[name] = "passed"
    require(set(result) == expected, "GoogleTest result count/names mismatch (including zero executed cases)")
    return result


def case_counts(cases):
    counts = Counter(case["status"] for case in cases)
    return {state: counts[state] for state in ("passed", "failed", "skipped", "not_run")}


def run_regression(executor, extra_env=None):
    listed = run_process(["ctest", "--test-dir", str(executor.build), "--show-only=json-v1"],
                         executor.build, executor.timeout, extra_env)
    require(listed.returncode == 0, "full CTest enumeration failed")
    try:
        tests = json.loads(listed.stdout)["tests"]
    except (ValueError, KeyError, TypeError) as exc:
        raise RunnerError("invalid CTest enumeration") from exc
    require(isinstance(tests, list) and tests, "full CTest regression has zero registered cases")
    names = unique_strings([test.get("name") for test in tests], "CTest names")
    for test in tests:
        properties = {prop["name"]: prop["value"] for prop in test.get("properties", [])}
        require(not properties.get("DISABLED") and not properties.get("WILL_FAIL"),
                f"disabled/inverted CTest result cannot be accepted: {test['name']}")
        command = test.get("command", [])
        require(bool(command), f"CTest has no executable command: {test['name']}")
        filters = [arg.removeprefix("--gtest_filter=") for arg in command if arg.startswith("--gtest_filter=")]
        if filters:
            require(len(filters) == 1, f"ambiguous CTest GoogleTest filter: {test['name']}")
            binary_path = Path(command[0]).resolve()
            require(binary_path.is_relative_to(executor.build), "CTest GoogleTest binary is outside build")
            binary = str(binary_path.relative_to(executor.build))
            registered = test_ids([{"binary": binary, "name": name} for name in filters[0].split(":")], test["name"])
            executor.preflight([registered])
    with tempfile.TemporaryDirectory(prefix="labs-regression-", dir=executor.build) as temp:
        output = Path(temp) / "ctest.xml"
        result = run_process(["ctest", "--test-dir", str(executor.build), "--output-on-failure",
                              "--no-tests=error", "--parallel", str(jobs()), "--timeout", str(executor.timeout),
                              "--output-junit", str(output)], executor.build, None, extra_env)
        records = {name: {"name": name, "status": "not_run"} for name in names}
        error = None
        try:
            document = ET.parse(output)
            seen = set()
            for case in document.iter("testcase"):
                name = case.get("name")
                require(name in records and name not in seen, f"unexpected/duplicate CTest result: {name}")
                seen.add(name)
                status = "passed"
                if case.find("skipped") is not None or case.get("status") == "notrun":
                    status = "skipped"
                elif case.find("failure") is not None or case.find("error") is not None:
                    status = "failed"
                records[name]["status"] = status
            require(seen == set(names), "CTest result count/names mismatch")
        except (OSError, ET.ParseError, RunnerError) as exc:
            error = str(exc)
        cases = list(records.values())
        passed = result.returncode == 0 and error is None and all(case["status"] == "passed" for case in cases)
        return {"status": "passed" if passed else "failed", "discovered_cases": len(names),
                "cases": cases, "case_counts": case_counts(cases), "returncode": result.returncode,
                "report_error": error, "output": "" if passed else (result.stdout + result.stderr)[-12000:]}


def initial_report(catalog, command, selected, weights):
    return {"schema_version": 2, "command": command, "selected_questions": selected,
            "questions": {qid: {"status": "not_run" if qid in selected else "not_selected",
                                "weight": weights.get(qid, 0), "discovered_cases": 0, "cases": []}
                          for qid in sorted(catalog.questions)}, "profiles": [], "errors": []}


def finalize_report(report):
    questions = report["questions"]
    counts = Counter(value["status"] for value in questions.values())
    selected = [questions[qid] for qid in report["selected_questions"]]
    report["question_counts"] = {state: counts[state] for state in ("passed", "failed", "not_run", "not_selected")}
    report["discovered_unit_cases"] = sum(value["discovered_cases"] for value in selected)
    report["unit_case_counts"] = case_counts([case for value in selected for case in value["cases"]])
    report["score"] = {"earned": sum(value["weight"] for value in selected if value["status"] == "passed"),
                       "possible": sum(value["weight"] for value in selected)}
    report["stages"] = {}
    for stage in sorted({qid[1:3] for qid in questions}):
        stage_results = [value for qid, value in questions.items() if qid[1:3] == stage]
        report["stages"][stage] = {"total": len(stage_results),
                                  "selected": sum(value["status"] != "not_selected" for value in stage_results),
                                  "passed": sum(value["status"] == "passed" for value in stage_results)}
    return report


def execute(catalog, args, root):
    choices = parse_choices(getattr(args, "choice", []))
    weights, requested = {}, []
    if getattr(args, "file", None):
        selected, weights, requested, file_choices = read_selection(args.file, catalog)
        file_choices.update(choices)
        choices = file_choices
    elif args.command == "unit":
        selected = catalog.check_questions(args.questions)
    elif args.command in ("plan", "integrate", "all"):
        selected = None
    else:
        selected = []
    profile_id = "db-reference" if args.command == "all" else getattr(args, "profile", None)
    profile_plan = plan(catalog, profile_id, choices, selected) if profile_id else None
    if selected is None:
        selected = profile_plan["required_questions"]
    weights = weights or {qid: catalog.weights[qid] for qid in selected}
    report = initial_report(catalog, args.command, selected, weights)
    if profile_plan:
        report["profiles"].append(profile_plan)
    elif args.command == "selection":
        report["profiles"] = [plan(catalog, pid, choices, selected) for pid in requested]
    if args.command == "plan":
        return finalize_report(report), 0
    exit_code = 0
    try:
        catalog.validate_choices(choices)
        capability_tests, integration_tests, scaffold_tests = [], [], []
        if args.command in ("integrate", "all"):
            require(profile_plan["mode"] == "integration", "integrate requires a database profile")
            require(not profile_plan["missing_questions"], "integration refuses unselected requirements: " + ", ".join(profile_plan["missing_questions"]))
            capability_tests, integration_tests = catalog.integration(profile_id)
        run_questions = profile_plan["required_questions"] if args.command in ("integrate", "all") else selected
        groups = {qid: catalog.unit_tests(qid) for qid in run_questions}
        if args.command == "scaffold":
            scaffold = catalog.manifest.get("scaffold", {})
            require(isinstance(scaffold, dict), "scaffold must be an object")
            scaffold_tests = test_ids(scaffold.get("test_ids"), "scaffold")
        build = args.build_dir.resolve()
        require(build != root.resolve(), "in-source builds are not supported")
        with build_lock(build):
            report["build"] = {"status": "not_run", "reason": "--no-build; use existing binaries"}
            if not getattr(args, "no_build", False):
                report["build"] = build_project(root, build)
            if report["build"]["status"] == "failed":
                return finalize_report(report), 1
            executor = Executor(build, args.timeout)
            all_groups = [*groups.values()]
            all_groups.extend(group for group in [capability_tests, integration_tests, scaffold_tests] if group)
            try:
                executor.preflight(all_groups)
            finally:
                for qid, tests in groups.items():
                    found = [test for test in tests if test.name in executor.discovered.get(test.binary, set())]
                    report["questions"][qid]["discovered_cases"] = len(found)
                    report["questions"][qid]["cases"] = [{**asdict(test), "status": "not_run"} for test in found]
            for qid, tests in groups.items():
                result = executor.run_group(tests, {"LABS_QUESTION": qid})
                report["questions"][qid].update(result)
                if result["status"] != "passed":
                    exit_code = 1
            if scaffold_tests:
                report["scaffold"] = executor.run_group(scaffold_tests)
                exit_code = int(report["scaffold"]["status"] != "passed")
            if profile_plan and args.command in ("integrate", "all"):
                unaccepted = [qid for qid in profile_plan["required_questions"] if report["questions"][qid]["status"] != "passed"]
                profile_plan["unaccepted_questions"] = unaccepted
                if unaccepted:
                    profile_plan["status"] = "unaccepted_questions"
                    exit_code = 1
                else:
                    env = {"LABS_PROFILE": profile_id,
                           "LABS_CAPABILITIES": json.dumps(profile_plan["capabilities"]),
                           "LABS_CHOICES": json.dumps(profile_plan["choices"], sort_keys=True),
                           "LABS_SELECTED_QUESTIONS": json.dumps(profile_plan["required_questions"])}
                    evidence = executor.run_group(capability_tests, env)
                    profile_plan["capability_checks"] = evidence
                    profile_plan["product_capabilities"] = evidence["status"]
                    if evidence["status"] == "passed":
                        integration = executor.run_group(integration_tests, env)
                        profile_plan["integration"] = integration
                        profile_plan["status"] = integration["status"]
                        if integration["status"] == "passed" and profile_id == "db-reference":
                            profile_plan["status"] = "not_run"
                            profile_plan["full_regression"] = {"status": "not_run", "discovered_cases": 0}
                            regression = run_regression(executor, env)
                            profile_plan["full_regression"] = regression
                            profile_plan["status"] = regression["status"]
                    else:
                        profile_plan["status"] = "failed"
                    if profile_plan["status"] != "passed":
                        exit_code = 1
    except (RunnerError, OSError) as exc:
        report["errors"].append(str(exc))
        exit_code = 2
    for profile in report["profiles"]:
        if profile["status"] == "not_run":
            profile["unaccepted_questions"] = [qid for qid in profile["required_questions"]
                                               if report["questions"][qid]["status"] != "passed"]
    return finalize_report(report), exit_code


def print_group(label, group):
    print(f"{label}: {group['status']}; discovered cases {group.get('discovered_cases', 0)}; "
          f"case outcomes {group.get('case_counts', {})}")
    for process in group.get("processes", []):
        if process.get("report_error"):
            print("  " + process["report_error"], file=sys.stderr)
        if process.get("output"):
            print(process["output"], file=sys.stderr)
    if group.get("output"):
        print(group["output"], file=sys.stderr)


def print_report(report, json_output=False):
    if json_output:
        print(json.dumps(report, ensure_ascii=False, indent=2))
        return
    counts = report["question_counts"]
    print(f"Selected questions: {len(report['selected_questions'])}; passed {counts['passed']}; failed {counts['failed']}; "
          f"not run {counts['not_run']}; not selected {counts['not_selected']}")
    print(f"Discovered unit cases: {report['discovered_unit_cases']}; case outcomes {report['unit_case_counts']}")
    print(f"Score: {report['score']['earned']}/{report['score']['possible']}")
    for qid in report["selected_questions"]:
        value = report["questions"][qid]
        print_group("  " + qid, value)
    for stage, value in report["stages"].items():
        if value["selected"]:
            print(f"Stage {stage}: selected {value['selected']}/{value['total']}; passed {value['passed']}")
    for profile in report["profiles"]:
        print(f"Profile {profile['profile']}: {profile['status']}; product capabilities {profile['product_capabilities']}")
        print("  Required questions: " + ", ".join(profile["required_questions"]))
        print("  Choices: " + json.dumps(profile["choices"], ensure_ascii=False))
        print("  Integration design: " + ", ".join(profile["integration_design"]))
        for test in profile["integration_test_ids"]:
            print(f"  Test: {test['binary']}::{test['name']}")
        if profile["missing_questions"]:
            print("  Missing selected questions: " + ", ".join(profile["missing_questions"]))
        for error in profile["registration_errors"]:
            print("  Registration: " + error)
        for key in ("capability_checks", "integration", "full_regression"):
            if key in profile:
                print_group("  " + key, profile[key])
    if "build" in report:
        print("build: " + report["build"]["status"])
    if "scaffold" in report:
        print_group("scaffold", report["scaffold"])
    for error in report["errors"]:
        print("runner error: " + error, file=sys.stderr)


def parser():
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--build-dir", type=Path, default=Path(os.getenv("LABS_BUILD_DIR", str(ROOT / "build/gcc-release"))))
    common.add_argument("--report", type=Path, help="write the complete JSON report")
    common.add_argument("--json", action="store_true", help="print JSON instead of the text report")
    common.add_argument("--timeout", type=float, default=300, help="timeout in seconds per test process")
    result = argparse.ArgumentParser(description=__doc__)
    commands = result.add_subparsers(dest="command", required=True)
    commands.add_parser("scaffold", parents=[common])
    listing = commands.add_parser("list", parents=[common])
    listing.add_argument("--stage", type=int, choices=range(1, 11))
    for name in ("unit", "selection", "plan", "integrate", "all"):
        command = commands.add_parser(name, parents=[common])
        if name != "plan":
            command.add_argument("--no-build", action="store_true", help="validate and use existing binaries")
        if name == "unit":
            command.add_argument("questions", nargs="+")
        if name in ("selection", "plan", "integrate"):
            command.add_argument("--file", type=Path, required=name == "selection", help="explicit course selection; never expanded")
            command.add_argument("--choice", action="append", default=[], metavar="CAPABILITY=QUESTION")
        if name in ("plan", "integrate"):
            command.add_argument("--profile", required=True)
    return result


def main(argv=None, root=ROOT):
    args = parser().parse_args(argv)
    try:
        require(math.isfinite(args.timeout) and args.timeout > 0, "--timeout must be finite and positive")
        catalog = Catalog.load(root)
        if args.command == "list":
            entries = []
            for qid, question in catalog.questions.items():
                if args.stage is None or question["stage"] == args.stage:
                    entries.append({"id": qid, "stage": question["stage"], "title": question.get("title", ""),
                                    "paths": question.get("paths", []), "fixtures": question.get("fixtures", []),
                                    "weight": catalog.weights[qid], "scope": question.get("algorithm_scope", ""),
                                    "required_checks": question.get("required_checks", []),
                                    "test_ids": question.get("unit", {}).get("test_ids", []),
                                    "registration": "registered" if question.get("unit", {}).get("test_ids") else "missing"})
            if args.json:
                print(json.dumps(entries, ensure_ascii=False, indent=2))
            else:
                for entry in entries:
                    print(f"{entry['id']} {entry['title']} (weight {entry['weight']}; tests {entry['registration']})")
                    print("  Source: " + ", ".join(entry["paths"]))
                    print("  Fixtures: " + ", ".join(entry["fixtures"]))
                    print("  Scope: " + entry["scope"])
                    print("  Checks: " + "; ".join(entry["required_checks"]))
            if args.report:
                args.report.parent.mkdir(parents=True, exist_ok=True)
                args.report.write_text(json.dumps(entries, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
            return 0
        report, exit_code = execute(catalog, args, root)
        report["exit_code"] = exit_code
        if args.report:
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print_report(report, args.json)
        return exit_code
    except (RunnerError, OSError) as exc:
        print("runner error: " + str(exc), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
