"""Reproducible session schedule with explicit async send/await barriers."""
import argparse,json,pathlib,time
from concurrent.futures import ThreadPoolExecutor
from rmdb_text import Client,check

def run(spec,host,port,timeout):
    clients={};pending={};events=[];pool=ThreadPoolExecutor(max_workers=8)
    try:
        for i,step in enumerate(spec['steps']):
            sid=step['session'];c=clients.setdefault(sid,None)
            if c is None:c=clients[sid]=Client(host,port,timeout)
            action=step.get('action','execute');event={'step':i+1,**step}
            if action=='send':
                if sid in pending:raise ValueError('One pending request per connection')
                pending[sid]=pool.submit(c.execute,step['sql']);event['dispatched']=True
            else:
                if action=='await':reply=pending.pop(sid).result(timeout=timeout+1)
                elif action=='execute':
                    if sid in pending:raise ValueError('Await pending request first')
                    reply=c.execute(step['sql'])
                else:raise ValueError(action)
                event['response']=reply
                if 'expect' in step:check(reply,step['expect'])
            events.append(event)
        if pending:raise ValueError('Unawaited operations at schedule end')
        return {'passed':True,'events':events}
    except Exception as e:return {'passed':False,'events':events,'error':repr(e)}
    finally:
        for c in clients.values():
            if c:c.close()
        pool.shutdown(wait=True,cancel_futures=True)

def main():
    p=argparse.ArgumentParser();p.add_argument('scenario',type=pathlib.Path)
    p.add_argument('--host',default='127.0.0.1');p.add_argument('--port',type=int,default=8765)
    p.add_argument('--timeout',type=float,default=5);p.add_argument('--output',type=pathlib.Path,required=True)
    a=p.parse_args()
    if a.output.exists():p.error('Result exists; choose a new path')
    result=run(json.loads(a.scenario.read_text(encoding='utf-8')),a.host,a.port,a.timeout)
    result.update(protocol='legacy-text',official=False)
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print('PASS' if result['passed'] else 'FAIL');raise SystemExit(0 if result['passed'] else 1)
if __name__=='__main__':main()
