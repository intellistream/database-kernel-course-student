"""Teaching/qualifier text protocol ONLY. Not 2026 final Wire Protocol."""
import socket, time
from collections import Counter
from decimal import Decimal, InvalidOperation

class Client:
    def __init__(self,host='127.0.0.1',port=8765,timeout=5):
        self.socket=socket.create_connection((host,port),timeout)
        self.timeout=timeout; self.pending=b''
    def execute(self,sql):
        if '\0' in sql: raise ValueError('NUL in SQL')
        self.socket.sendall(sql.encode('utf-8')+b'\0')
        deadline=time.monotonic()+self.timeout
        while b'\0' not in self.pending:
            remaining=deadline-time.monotonic()
            if remaining<=0: raise TimeoutError('Incomplete RMDB response')
            self.socket.settimeout(remaining)
            part=self.socket.recv(65536)
            if not part: raise EOFError('Server closed before NUL terminator')
            self.pending+=part
            if len(self.pending)>16*1024*1024: raise ValueError('Teaching response exceeds 16MiB')
        reply,self.pending=self.pending.split(b'\0',1)
        return reply.decode('utf-8',errors='strict')
    def close(self): self.socket.close()

def table(reply):
    """Ignore borders/row-count decoration; retain all repeated data rows."""
    rows=[]
    for line in reply.splitlines():
        line=line.strip()
        if line.startswith('|') and line.endswith('|'):
            cells=[v.strip() for v in line[1:-1].split('|')]
            if all(v and set(v)<=set('-+=: ') for v in cells):continue
            rows.append(cells)
    if not rows: raise AssertionError('No table header in response: '+repr(reply))
    return rows[0],rows[1:]

def check(reply,expect):
    if 'contains' in expect:
        if expect['contains'].lower() not in reply.lower():raise AssertionError(reply)
        return
    if expect.get('success'):
        if any(w in reply.lower() for w in ['error','failure','abort','not found','does not exist']):
            raise AssertionError(reply)
        return
    header,rows=table(reply)
    if header!=expect['columns']:raise AssertionError(f'columns: {header} != {expect["columns"]}')
    numeric=set(expect.get('numeric_columns',[]))
    def normalize(row):
        if len(row)!=len(header):raise AssertionError('Column count mismatch')
        vals=[]
        for i,v in enumerate(row):
            if i in numeric:
                try:
                    d=Decimal(str(v))
                    if not d.is_finite():raise AssertionError('Nonfinite numeric value')
                    vals.append(d)
                except InvalidOperation:raise AssertionError('Invalid number '+str(v))
            else:vals.append(str(v))
        return tuple(vals)
    actual=list(map(normalize,rows)); desired=list(map(normalize,expect['rows']))
    ok=actual==desired if expect.get('ordered',False) else Counter(actual)==Counter(desired)
    if not ok:raise AssertionError(f'rows: {actual} != {desired}')
