import unittest,sys,pathlib,socket,threading,json,tempfile,subprocess,time
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
from rmdb_text import Client,check
from summarize import summarize
from run_schedule import run

class HarnessTest(unittest.TestCase):
    def server(self,chunks):
        server=socket.socket();server.bind(('127.0.0.1',0));server.listen(1)
        port=server.getsockname()[1]
        def work():
            conn,_=server.accept()
            with conn:
                data=b''
                while b'\0' not in data:data+=conn.recv(4096)
                for chunk in chunks:conn.sendall(chunk)
            server.close()
        t=threading.Thread(target=work);t.start();return port,t
    def test_fragmented_response(self):
        port,t=self.server([b'| a |\n| ',b'1 |\n',b'\0'])
        c=Client(port=port);self.assertEqual(c.execute('select a;'),'| a |\n| 1 |\n');c.close();t.join()
    def test_incomplete_response_fails(self):
        port,t=self.server([b'| a |'])
        c=Client(port=port)
        with self.assertRaises(EOFError):c.execute('select a;')
        c.close();t.join()
    def test_bag_keeps_duplicates(self):
        with self.assertRaises(AssertionError):check('| id |\n| 1 |\n',{'columns':['id'],'rows':[['1'],['1']]})
    def test_unordered_and_numeric(self):
        check('| id |\n| 2.000000 |\n| 1 |',{'columns':['id'],'rows':[[1],[2]],'numeric_columns':[0]})
    def test_ordered(self):
        with self.assertRaises(AssertionError):check('| id |\n| 2 |\n| 1 |',{'columns':['id'],'rows':[['1'],['2']],'ordered':True})
    def test_empty_requires_header(self):
        with self.assertRaises(AssertionError):check('',{'columns':['id'],'rows':[]})
    def test_string_not_numeric(self):
        with self.assertRaises(AssertionError):check('| id |\n| 01 |',{'columns':['id'],'rows':[['1']]})
    def test_correctness_gate(self):
        with self.assertRaises(ValueError):summarize([{'correct':False,'seconds':150,'committed_neworder':9999}]*3)
    def test_median(self):
        r=summarize([{'correct':True,'seconds':150,'committed_neworder':n} for n in [100,200,900]])
        self.assertEqual(r['median'],80)
    def test_cli_e2e(self):
        port,t=self.server([b'| id |\n| 1 |\n\0'])
        with tempfile.TemporaryDirectory() as d:
            p=pathlib.Path(d);(p/'case.json').write_text(json.dumps([{'sql':'select id;','expect':{'columns':['id'],'rows':[['1']]}}]))
            tool=pathlib.Path(__file__).resolve().parents[1]/'tools/run_cases.py'
            result=subprocess.run([sys.executable,str(tool),str(p/'case.json'),'--port',str(port),'--output',str(p/'result.json')],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr);self.assertTrue(json.loads((p/'result.json').read_text())['passed'])
        t.join()
    def test_receive_timeout(self):
        server=socket.socket();server.bind(('127.0.0.1',0));server.listen(1)
        port=server.getsockname()[1]
        def stall():
            conn,_=server.accept()
            with conn:
                conn.recv(4096);time.sleep(0.2)
            server.close()
        t=threading.Thread(target=stall);t.start()
        c=Client(port=port,timeout=0.05)
        with self.assertRaises(TimeoutError):c.execute('select id;')
        c.close();t.join()
    def test_send_await_allows_other_session(self):
        server=socket.socket();server.bind(('127.0.0.1',0));server.listen(2)
        port=server.getsockname()[1];released=threading.Event();workers=[]
        def reply(conn):
            with conn:
                data=b''
                while b'\0' not in data:data+=conn.recv(4096)
                if data.startswith(b'wait'):released.wait(1)
                else:released.set()
                conn.sendall(b'OK\0')
        def accept():
            for _ in range(2):
                c,_=server.accept();t=threading.Thread(target=reply,args=(c,));t.start();workers.append(t)
            for t in workers:t.join()
            server.close()
        t=threading.Thread(target=accept);t.start()
        result=run({'steps':[{'session':'A','action':'send','sql':'wait;'},
            {'session':'B','sql':'release;'}, {'session':'A','action':'await'}]},'127.0.0.1',port,0.4)
        self.assertTrue(result['passed'],result);self.assertTrue(released.is_set());t.join()
if __name__=='__main__':unittest.main()
