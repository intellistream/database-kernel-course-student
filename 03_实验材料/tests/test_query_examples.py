"""Cross-check hand-written expectations using the common SQL subset only.

This validates the worksheet data, not RMDB or official evaluator compatibility.
"""
import json,pathlib,sqlite3,unittest
from collections import Counter

class QueryExamples(unittest.TestCase):
    def test_join_and_aggregate_expectations(self):
        source=pathlib.Path(__file__).resolve().parents[1]/'cases/query.json'
        with sqlite3.connect(':memory:') as db:
            for case in json.loads(source.read_text(encoding='utf-8')):
                with self.subTest(sql=case['sql']):
                    cursor=db.execute(case['sql']);expect=case['expect']
                    if 'rows' not in expect:continue
                    self.assertEqual([c[0] for c in cursor.description],expect['columns'])
                    actual=cursor.fetchall();desired=list(map(tuple,expect['rows']))
                    if expect.get('ordered'):self.assertEqual(actual,desired)
                    else:self.assertEqual(Counter(actual),Counter(desired))
