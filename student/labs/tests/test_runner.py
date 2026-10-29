"""Runner policy tests; process fixtures exercise no database algorithms."""

import copy
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import run as runner


def registry(name):
    return {"binary": "runner_fixture", "name": name}


def manifest():
    return {
        "schema_version": 2,
        "questions": [{"id": f"S01.Q0{number}", "stage": 1, "title": str(number),
                       "unit": {"test_ids": [registry("Cases.Pass")]}}
                      for number in (1, 2, 3)],
        "capabilities": [
            {"id": "storage.replacement", "requirements": {"any": ["S01.Q01", "S01.Q02"]}},
            {"id": "database.basic", "requirements": {"all": ["storage.replacement", "S01.Q03"]}},
            {"id": "database.reference", "requirements": {"all": ["S01.Q01", "S01.Q02", "S01.Q03"]}},
        ],
        "profiles": [
            {"id": "basic", "mode": "integration", "requires": ["database.basic"],
             "default_choices": {"storage.replacement": "S01.Q01"}},
            {"id": "db-reference", "mode": "integration", "requires": ["database.reference"]},
            {"id": "sample", "mode": "unit", "selection": ["S01.Q01"]},
        ],
        "integration": {"profiles": {
            pid: {"kind": "database_session", "test_ids": [registry("Cases.SQL")],
                  "capability_test_ids": [registry("Cases.Capabilities")], "full_regression": True}
            for pid in ("basic", "db-reference")
        }},
    }


PROCESS_FIXTURE = r'''
import json
import os
from pathlib import Path
import sys
import xml.etree.ElementTree as ET

if '--gtest_list_tests' in sys.argv:
    print('Cases.\n  Pass\n  Fail\n  Skip\n  Abort\n  Zero\n  Mismatch\n  Capabilities\n  SQL')
    raise SystemExit(0)
names = next(arg.split('=', 1)[1] for arg in sys.argv if arg.startswith('--gtest_filter=')).split(':')
with Path('calls.jsonl').open('a') as log:
    log.write(json.dumps({'pid': os.getpid(), 'names': names, 'question': os.getenv('LABS_QUESTION'),
                          'profile': os.getenv('LABS_PROFILE'), 'choices': os.getenv('LABS_CHOICES'),
                          'gtest_env': [key for key in os.environ if key.startswith('GTEST_')]}) + '\n')
if 'Cases.Abort' in names:
    print('TODO algorithm missing', flush=True)
    os._exit(17)
output = next(arg.split('xml:', 1)[1] for arg in sys.argv if arg.startswith('--gtest_output='))
document = ET.Element('testsuites')
suite = ET.SubElement(document, 'testsuite', name='Cases')
for name in names:
    if name == 'Cases.Zero':
        continue
    case = ET.SubElement(suite, 'testcase', classname='Cases', name=name.split('.')[1],
                         status='run', result='completed')
    if name == 'Cases.Mismatch':
        case.set('name', 'Unexpected')
    if name == 'Cases.Fail':
        ET.SubElement(case, 'failure').text = 'positive algorithm assertion failed'
    if name == 'Cases.Skip':
        ET.SubElement(case, 'skipped').text = 'not an acceptance result'
ET.ElementTree(document).write(output)
raise SystemExit(1 if 'Cases.Fail' in names else 0)
'''


class CatalogTests(unittest.TestCase):
    def test_real_profiles_expand_clock_lru_and_reference(self):
        catalog = runner.Catalog.load(runner.ROOT)
        for pid, count in (("db-basic-rc", 46), ("db-relational", 76)):
            clock, _, _ = catalog.expand(pid)
            lru, _, choices = catalog.expand(pid, {"storage.replacement": "S01.Q03"})
            self.assertEqual(len(clock), count)
            self.assertEqual(len(lru), count)
            self.assertEqual(set(clock) ^ set(lru), {"S01.Q02", "S01.Q03"})
            self.assertEqual(choices, {"storage.replacement": "S01.Q03"})
        self.assertEqual(len(catalog.expand("db-reference")[0]), 83)
        self.assertEqual(len(catalog.expand("sample-10")[0]), 10)

    def test_dag_rejects_invalid_and_unselected_cyclic_branches(self):
        for mutation in ("unknown", "cycle", "empty", "both"):
            with self.subTest(mutation=mutation):
                data = manifest()
                if mutation == "unknown":
                    data["capabilities"][0]["requirements"]["any"].append("missing")
                elif mutation == "cycle":
                    data["capabilities"][0]["requirements"]["any"].append("database.basic")
                elif mutation == "empty":
                    data["capabilities"][0]["requirements"] = {"any": []}
                else:
                    data["capabilities"][0]["requirements"]["all"] = ["S01.Q01"]
                with self.assertRaises(runner.RunnerError):
                    runner.Catalog(data)

    def test_choices_are_explicit_valid_and_used(self):
        catalog = runner.Catalog(manifest())
        for choices in ({"unknown": "S01.Q01"}, {"database.basic": "S01.Q01"},
                        {"storage.replacement": "S01.Q03"}):
            with self.subTest(choices=choices), self.assertRaises(runner.RunnerError):
                catalog.expand("basic", choices)
        with self.assertRaisesRegex(runner.RunnerError, "does not apply"):
            catalog.expand("sample", {"storage.replacement": "S01.Q01"})
        data = manifest()
        data["profiles"][0].pop("default_choices")
        with self.assertRaisesRegex(runner.RunnerError, "choice required"):
            runner.Catalog(data).expand("basic")
        with self.assertRaises(runner.RunnerError):
            runner.parse_choices(["storage.replacement=S01.Q01", "storage.replacement=S01.Q02"])

    def test_plan_does_not_expand_course_selection(self):
        selected = ["S01.Q02"]
        result = runner.plan(runner.Catalog(manifest()), "basic", {}, selected)
        self.assertEqual(selected, ["S01.Q02"])
        self.assertEqual(result["selected_questions"], selected)
        self.assertEqual(result["missing_questions"], ["S01.Q01", "S01.Q03"])
        self.assertEqual(result["product_capabilities"], "unverified")

    def test_duplicate_regions_and_invalid_ids_fail(self):
        data = manifest()
        region = {"id": "S01.Q01:R01", "owner": "S01.Q01"}
        data["questions"][0]["regions"] = [region, copy.deepcopy(region)]
        with self.assertRaisesRegex(runner.RunnerError, "duplicate region"):
            runner.Catalog(data)
        data = manifest()
        data["questions"][0]["id"] = "S1.Q1"
        with self.assertRaisesRegex(runner.RunnerError, "invalid question"):
            runner.Catalog(data)

    def test_registration_rejects_filters_disabled_empty_and_escaping_paths(self):
        invalid = [[], [registry("Cases.*")], [registry("Cases.Pass:Cases.Fail")],
                   [registry("Cases.DISABLED_Pass")], [registry("DISABLED_Cases.Pass")],
                   [registry("Cases.Pass"), registry("Cases.Pass")],
                   [{"binary": "../outside", "name": "Cases.Pass"}]]
        for entry in invalid:
            with self.subTest(entry=entry), self.assertRaises(runner.RunnerError):
                runner.test_ids(entry, "test")

    def test_listing_preserves_parameter_names_and_rejects_zero(self):
        listing = "Running main() from test.cc\nTyped/0. # TypeParam = int\n  Reads\nRows/Suite.\n  Checks/Small # GetParam() = 2\n"
        self.assertEqual(runner.parse_gtest_listing(listing), {"Typed/0.Reads", "Rows/Suite.Checks/Small"})
        with self.assertRaisesRegex(runner.RunnerError, "zero"):
            runner.parse_gtest_listing("Running main() only\n")


class ExecutionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="labs-runner-test-")
        self.addCleanup(self.temp.cleanup)
        self.build = Path(self.temp.name) / "build"
        self.build.mkdir()
        binary = self.build / "runner_fixture"
        binary.write_text("#!" + sys.executable + "\n" + PROCESS_FIXTURE, encoding="utf-8")
        binary.chmod(0o755)
        self.data = manifest()

    def execute(self, *command):
        args = runner.parser().parse_args([*command, "--build-dir", str(self.build), "--no-build"])
        return runner.execute(runner.Catalog(self.data), args, Path(self.temp.name))

    def set_case(self, index, case):
        self.data["questions"][index]["unit"]["test_ids"] = [registry(case)]

    def calls(self):
        file = self.build / "calls.jsonl"
        return [json.loads(line) for line in file.read_text().splitlines()] if file.exists() else []

    def selection_file(self, selected, **extra):
        path = Path(self.temp.name) / "selection.json"
        value = {"schema_version": 2, "selected_questions": selected,
                 "weights": {qid: 2 for qid in selected}, **extra}
        path.write_text(json.dumps(value), encoding="utf-8")
        return path

    def test_questions_run_separately_and_failure_does_not_stop_next_question(self):
        self.set_case(0, "Cases.Fail")
        report, code = self.execute("unit", "S01.Q01", "S01.Q02")
        self.assertEqual(code, 1)
        self.assertEqual(report["question_counts"], {"passed": 1, "failed": 1, "not_run": 0, "not_selected": 1})
        self.assertEqual(report["discovered_unit_cases"], 2)
        calls = self.calls()
        self.assertEqual([call["question"] for call in calls], ["S01.Q01", "S01.Q02"])
        self.assertNotEqual(calls[0]["pid"], calls[1]["pid"])

    def test_abort_is_failure_with_unreported_cases_and_next_question_runs(self):
        self.set_case(0, "Cases.Abort")
        report, code = self.execute("unit", "S01.Q01", "S01.Q02")
        self.assertEqual(code, 1)
        self.assertEqual(report["question_counts"]["failed"], 1)
        self.assertEqual(report["questions"]["S01.Q01"]["processes"][0]["returncode"], 17)
        self.assertEqual(report["unit_case_counts"], {"passed": 1, "failed": 0, "skipped": 0, "not_run": 1})

    def test_zero_skip_and_mismatched_results_cannot_pass(self):
        for case in ("Cases.Zero", "Cases.Skip", "Cases.Mismatch"):
            with self.subTest(case=case):
                self.set_case(0, case)
                report, code = self.execute("unit", "S01.Q01")
                self.assertEqual(code, 1)
                self.assertEqual(report["questions"]["S01.Q01"]["status"], "failed")
                self.assertEqual(report["score"]["earned"], 0)
                self.assertEqual(report["discovered_unit_cases"], 1)

    def test_unknown_test_preflight_stops_all_test_execution(self):
        self.set_case(1, "Cases.Unknown")
        report, code = self.execute("unit", "S01.Q01", "S01.Q02")
        self.assertEqual(code, 2)
        self.assertIn("zero-match", report["errors"][0])
        self.assertEqual(report["question_counts"]["not_run"], 2)
        self.assertEqual(self.calls(), [])

    def test_missing_registration_fails_before_build_and_preserves_not_selected(self):
        self.data["questions"][0].pop("unit")
        with mock.patch.object(runner, "build_project", side_effect=AssertionError("must not build")):
            report, code = self.execute("unit", "S01.Q01")
        self.assertEqual(code, 2)
        self.assertEqual(report["question_counts"]["not_selected"], 2)
        self.assertEqual(report["discovered_unit_cases"], 0)
        self.assertEqual(self.calls(), [])

    def test_external_gtest_sharding_and_filter_are_removed(self):
        with mock.patch.dict(os.environ, {"GTEST_TOTAL_SHARDS": "100", "GTEST_SHARD_INDEX": "99", "GTEST_FILTER": "Nothing.*"}):
            report, code = self.execute("unit", "S01.Q01")
        self.assertEqual(code, 0)
        self.assertEqual(report["unit_case_counts"]["passed"], 1)
        self.assertEqual(self.calls()[0]["gtest_env"], [])

    def test_selection_is_weighted_and_never_runs_requested_integration(self):
        selection = self.selection_file(["S01.Q02"], requested_profiles=["basic"])
        report, code = self.execute("selection", "--file", str(selection))
        self.assertEqual(code, 0)
        self.assertEqual(report["score"], {"earned": 2, "possible": 2})
        self.assertEqual(report["profiles"][0]["status"], "missing_questions")
        self.assertEqual(report["selected_questions"], ["S01.Q02"])
        self.assertEqual(len(self.calls()), 1)

    def test_invalid_selection_ids_weights_and_duplicates_fail(self):
        catalog = runner.Catalog(self.data)
        for selected, extra in ((["S01.Q01", "S01.Q01"], {}), (["S02.Q99"], {}),
                                (["S01.Q01"], {"weights": {}}), (["S01.Q01"], {"weights": {"S01.Q01": 0}})):
            with self.subTest(selected=selected, extra=extra), self.assertRaises(runner.RunnerError):
                runner.read_selection(self.selection_file(selected, **extra), catalog)

    def test_integration_refuses_missing_selection_before_any_process(self):
        selection = self.selection_file(["S01.Q02"])
        report, code = self.execute("integrate", "--profile", "basic", "--file", str(selection))
        self.assertEqual(code, 2)
        self.assertIn("unselected requirements", report["errors"][0])
        self.assertEqual(self.calls(), [])
        self.assertEqual(report["selected_questions"], ["S01.Q02"])

    def test_capability_registration_is_required_and_units_must_pass_first(self):
        self.data["integration"]["profiles"]["basic"].pop("capability_test_ids")
        report, code = self.execute("integrate", "--profile", "basic")
        self.assertEqual(code, 2)
        self.assertEqual(report["profiles"][0]["product_capabilities"], "unverified")
        self.assertEqual(self.calls(), [])
        self.data = manifest()
        self.set_case(0, "Cases.Fail")
        report, code = self.execute("integrate", "--profile", "basic")
        self.assertEqual(code, 1)
        self.assertEqual(report["profiles"][0]["status"], "unaccepted_questions")
        self.assertTrue(all(call["question"] for call in self.calls()))

    def test_integration_runs_unit_then_capability_then_sql_with_recorded_choice(self):
        report, code = self.execute("integrate", "--profile", "basic", "--choice", "storage.replacement=S01.Q02")
        self.assertEqual(code, 0)
        self.assertEqual(report["selected_questions"], ["S01.Q02", "S01.Q03"])
        calls = self.calls()
        self.assertEqual([call["names"] for call in calls], [["Cases.Pass"], ["Cases.Pass"], ["Cases.Capabilities"], ["Cases.SQL"]])
        self.assertEqual(json.loads(calls[-1]["choices"]), {"storage.replacement": "S01.Q02"})
        self.assertEqual(report["profiles"][0]["product_capabilities"], "passed")

    def test_all_and_reference_both_require_original_regression(self):
        for command in (("all",), ("integrate", "--profile", "db-reference")):
            with self.subTest(command=command), mock.patch.object(runner, "run_regression", return_value={"status": "failed", "discovered_cases": 7}) as regression:
                report, code = self.execute(*command)
                self.assertEqual(code, 1)
                self.assertEqual(len(report["selected_questions"]), 3)
                self.assertEqual(report["profiles"][0]["full_regression"]["discovered_cases"], 7)
                regression.assert_called_once()
        with mock.patch.object(runner, "run_regression", side_effect=runner.RunnerError("zero regression cases")):
            report, code = self.execute("all")
        self.assertEqual(code, 2)
        self.assertEqual(report["profiles"][0]["status"], "not_run")
        self.assertEqual(report["profiles"][0]["full_regression"]["status"], "not_run")

    def test_parallelism_is_bounded_and_invalid_values_rejected(self):
        with mock.patch.dict(os.environ, {}, clear=True), mock.patch.object(os, "cpu_count", return_value=192):
            self.assertEqual(runner.jobs(), 16)
        for value in ("0", "17", "-1", "many"):
            with self.subTest(value=value), mock.patch.dict(os.environ, {"LABS_JOBS": value}), self.assertRaises(runner.RunnerError):
                runner.jobs()

    def test_scaffold_configures_all_headers_and_runs_only_registered_primitives(self):
        self.data["scaffold"] = {"test_ids": [registry("Cases.Pass")]}
        self.data["questions"][0].pop("unit")
        args = runner.parser().parse_args(["scaffold", "--build-dir", str(self.build)])
        with mock.patch.object(runner.subprocess, "run", return_value=mock.Mock(returncode=0)) as build:
            report, code = runner.execute(runner.Catalog(self.data), args, Path(self.temp.name))
        self.assertEqual(code, 0)
        self.assertEqual(build.call_count, 2)
        configure, compile_all = [call.args[0] for call in build.call_args_list]
        self.assertIn("-DBUILD_TESTING=ON", configure)
        self.assertIn("-DDB_WARNINGS_AS_ERRORS=ON", configure)
        self.assertNotIn("--target", compile_all)
        self.assertEqual(report["question_counts"]["not_selected"], 3)
        self.assertEqual(report["scaffold"]["discovered_cases"], 1)
        self.assertIsNone(self.calls()[0]["question"])

    def test_failed_capability_checks_block_sql_and_unused_course_question_is_not_run(self):
        self.data["integration"]["profiles"]["basic"]["capability_test_ids"] = [registry("Cases.Fail")]
        self.set_case(1, "Cases.Abort")
        selection = self.selection_file(["S01.Q01", "S01.Q02", "S01.Q03"])
        report, code = self.execute("integrate", "--profile", "basic", "--file", str(selection))
        self.assertEqual(code, 1)
        self.assertEqual(report["questions"]["S01.Q02"]["status"], "not_run")
        self.assertEqual(report["profiles"][0]["product_capabilities"], "failed")
        self.assertFalse(any("Cases.SQL" in call["names"] or "Cases.Abort" in call["names"] for call in self.calls()))

    def test_plan_is_read_only_and_does_not_include_private_metadata(self):
        self.data["baseline"] = {"private_metadata_sentinel": "must-never-be-printed"}
        args = runner.parser().parse_args(["plan", "--profile", "basic", "--build-dir", str(self.build)])
        with mock.patch.object(runner, "build_lock", side_effect=AssertionError("plan must be read only")):
            report, code = runner.execute(runner.Catalog(self.data), args, Path(self.temp.name))
        self.assertEqual(code, 0)
        self.assertNotIn("must-never-be-printed", json.dumps(report))
        self.assertFalse((self.build / "build.lock").exists())


if __name__ == "__main__":
    unittest.main()
