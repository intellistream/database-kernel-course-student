"""Release guard for course maintainers; not the grading command for student solutions."""
from pathlib import Path, PurePosixPath
from html.parser import HTMLParser
from urllib.parse import urlsplit, unquote
import hashlib, json, re, tarfile

ROOT=Path(__file__).resolve().parents[1]
def read(path):return json.loads(path.read_text(encoding='utf-8-sig'))
def digest(data):return hashlib.sha256(data).hexdigest()

class Page(HTMLParser):
    def __init__(self,text):
        super().__init__();self.ids=set();self.links=[];self.feed(text)
    def handle_starttag(self,tag,attrs):
        a=dict(attrs)
        if 'id' in a:
            assert a['id'] not in self.ids,a['id']
            self.ids.add(a['id'])
        if tag in {'a','img','script','link'}:
            link=a.get('href',a.get('src'))
            if link:self.links.append(link)

def main():
    release=read(ROOT/'student-release.json')
    expected={r['path'] for r in release['files']}|{'student-release.json'}
    actual={p.relative_to(ROOT).as_posix() for p in ROOT.rglob('*') if p.is_file() and not set(p.relative_to(ROOT).parts)&{'.git','__pycache__'}}
    assert actual==expected,{'unexpected':sorted(actual-expected),'missing':sorted(expected-actual)}
    for row in release['files']:
        data=(ROOT/row['path']).read_bytes()
        assert len(data)==row['bytes'] and digest(data)==row['sha256'],row['path']
    for name in actual:
        assert not any(x in name for x in ['Ferrum与决赛评测材料_','Ferrum-cpp.tar.gz','Ferrum.tar.gz','grader.tar.gz','redesign-proposal.md','curriculum-6-plus-3.md']),name
    base=ROOT/'04_Ferrum实验基础'
    manifest=read(base/'学生源码清单.json')
    assert digest((base/manifest['archive']).read_bytes())==manifest['sha256']
    source={x['path']:x for x in manifest['files']};seen=set()
    with tarfile.open(base/manifest['archive']) as archive:
        for m in archive:
            assert m.isfile(),m.name
            parts=PurePosixPath(m.name).parts
            assert parts[0]=='Ferrum-cpp-student' and '..' not in parts and '.git' not in parts
            assert m.name not in seen;seen.add(m.name)
            data=archive.extractfile(m).read();row=source[m.name]
            assert digest(data)==row['sha256'] and len(data)==row['bytes'] and m.mode==int(row['mode'],8)
            assert data==(ROOT/'student'/m.name.split('/',1)[1]).read_bytes()
    assert seen==set(source)
    questions=read(ROOT/'student/labs/manifest.json')['questions'];regions=set()
    for q in questions:
        for name in [q['guide']]+q['paths']+q['existing_test_sources']:
            assert (ROOT/'student'/name).is_file(),name
        for region in q['regions']:
            text=(ROOT/'student'/region['path']).read_text(encoding='utf-8')
            begin=region['marker'];end=begin.replace('-BEGIN','-END')
            assert text.count(begin)==text.count(end)==1
            assert text.index(begin)<text.index(end)
            body=text.split(begin,1)[1].split(end,1)[0]
            assert 'missing_algorithm' in body or 'kUnsupported' in body,region['id']
            assert region['id'] not in regions;regions.add(region['id'])
    assert len(questions)==83 and len(regions)==2229
    for row in read(ROOT/'02_历年赛题/来源索引.json'):
        assert digest((ROOT/row['file']).read_bytes())==row['sha256']
    pages={p:Page(p.read_text(encoding='utf-8-sig')) for p in ROOT.rglob('*.html') if 'student' not in p.relative_to(ROOT).parts}
    links=0
    for path,page in pages.items():
        for link in page.links:
            url=urlsplit(link)
            if url.scheme or url.netloc:continue
            target=(path.parent/unquote(url.path)).resolve() if url.path else path
            assert target.is_relative_to(ROOT) and target.exists(),(path.name,link)
            if url.fragment and target in pages:assert unquote(url.fragment) in pages[target].ids,(path.name,link)
            links+=1
    for path in ROOT.rglob('*.md'):
        if 'student' in path.relative_to(ROOT).parts:continue
        text=re.sub(r'```.*?```','',path.read_text(encoding='utf-8-sig'),flags=re.S)
        for link in re.findall(r'\[[^\]]*\]\(([^)]+)\)',text):
            url=urlsplit(link.strip('<>'))
            if url.scheme or url.netloc:continue
            target=(path.parent/unquote(url.path)).resolve() if url.path else path
            assert target.is_relative_to(ROOT) and target.exists(),(path.name,link)
    print(json.dumps({'release_files':len(actual),'source_files':len(source),'questions':len(questions),'initial_stub_regions':len(regions),'html_links':links,'kernel_tests':'not executed'},ensure_ascii=False,indent=2))

if __name__=='__main__':main()
