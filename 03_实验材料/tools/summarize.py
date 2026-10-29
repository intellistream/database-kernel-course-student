"""Summarize original measurement windows. Never infer official scores."""
import argparse,json,statistics,pathlib
def summarize(windows):
    if len(windows)<3:raise ValueError('At least three measured windows required')
    rates=[]
    for w in windows:
        if w.get('correct') is not True:raise ValueError('Correctness gate failed or missing')
        if w['seconds']<=0 or w['committed_neworder']<0:raise ValueError('Invalid count or duration')
        rates.append(60*w['committed_neworder']/w['seconds'])
    return {'official_score':False,'metric':'local NewOrder/min','windows':rates,'median':statistics.median(rates)}
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('input',type=pathlib.Path);a=p.parse_args()
    print(json.dumps(summarize(json.loads(a.input.read_text(encoding='utf-8'))),indent=2))
