import argparse,json,time,pathlib
from rmdb_text import Client,check

def main():
    p=argparse.ArgumentParser(description='Original course cases; legacy text protocol only')
    p.add_argument('case',type=pathlib.Path);p.add_argument('--host',default='127.0.0.1')
    p.add_argument('--port',type=int,default=8765);p.add_argument('--timeout',type=float,default=5)
    p.add_argument('--output',type=pathlib.Path,required=True);a=p.parse_args()
    if a.output.exists():p.error('Result exists; choose a new output path')
    cases=json.loads(a.case.read_text(encoding='utf-8')); results=[];failed=False
    c=Client(a.host,a.port,a.timeout)
    try:
        for item in cases:
            start=time.perf_counter();row={'sql':item['sql']}
            try:
                reply=c.execute(item['sql']); row['response']=reply
                check(reply,item['expect']);row['passed']=True
            except Exception as e:row.update(passed=False,error=str(e));failed=True
            row['elapsed_s']=time.perf_counter()-start;results.append(row)
            if failed:break
    finally:c.close()
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps({'protocol':'legacy-text','official':False,'passed':not failed,'results':results},indent=2,ensure_ascii=False),encoding='utf-8')
    print('PASS' if not failed else 'FAIL',len(results),'statements');raise SystemExit(1 if failed else 0)
if __name__=='__main__':main()
