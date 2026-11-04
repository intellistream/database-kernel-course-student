"""Check curriculum, native slide objects, and public package contents."""
from pathlib import Path
import json, re, zipfile, xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[1]
BASE=ROOT/'01_课件（第6版）'
data=json.loads((BASE/'课程安排.json').read_text(encoding='utf-8'))
assert len(data)==5 and sum(m['hours'] for m in data)==40
lessons=[l for m in data for l in m['lessons']]
labs=[l for m in data for l in m['labs']]
assert [l['no'] for l in lessons]==list(range(1,21))
assert [l['no'] for l in labs]==list(range(1,11)) and sum(l['hours'] for l in labs)==40
assert [int(re.search(r'第(\d+)周',l['week'])[1]) for l in labs]==[4,5,6,7,8,9,10,11,12,14]
assert all(l['hours']==4 for l in labs)
for l in labs:
 pre=lessons[l['prerequisite_lesson']-1]
 assert int(re.search(r'第(\d+)周',pre['week'])[1])<=int(re.search(r'第(\d+)周',l['week'])[1])
for l in lessons:
 expected=f'第{3+(l["no"]-1)//2}周第{1+(l["no"]-1)%2}次' if l['no']<=16 else f'第{l["no"]-6}周'
 assert l['week']==expected and l['hours']==2,l
 assert set(l)=={'no','title','week','goals','questions','pages','hours'}
names={m['file'] for m in data}
assert {p.name for p in BASE.glob('*.ppt*')}==names
ns={'a':'http://schemas.openxmlformats.org/drawingml/2006/main','p':'http://schemas.openxmlformats.org/presentationml/2006/main'}
total=0;tables=0;connectors=0
for m in data:
 with zipfile.ZipFile(BASE/m['file']) as z:
  slides=sorted(n for n in z.namelist() if re.fullmatch(r'ppt/slides/slide\d+\.xml',n))
  assert len(slides)==m['slides'];total+=len(slides)
  texts=[]
  for n in slides:
   root=ET.fromstring(z.read(n));texts.extend(t.text or '' for t in root.findall('.//a:t',ns))
   tables+=len(root.findall('.//a:tbl',ns));connectors+=len(root.findall('.//p:cxnSp',ns))
   assert not root.findall('.//p:pic',ns),'Slides must retain editable evidence'
  alltext='\n'.join(texts)
  for teacher in ['ZHANG SHUHAO','谢美意','毛言粲']:assert teacher in alltext
  for l in m['lessons']:
   assert l['title'] in alltext
   assert 1<=l['pages'][0]<=l['pages'][1]<=m['slides']
  for n in z.namelist():
   if n.endswith('.xml'):
    value=z.read(n).decode('utf-8')
    assert '参考要点：' not in value and '课堂练习参考答案' not in value
assert total==sum(m['slides'] for m in data) and tables>30 and connectors>50
print(json.dumps(dict(modules=5,lessons=20,labs=10,slides=total,native_tables=tables,native_connectors=connectors),ensure_ascii=False))
