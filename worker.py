import os,sys,time,subprocess,json
from pathlib import Path
from PIL import Image
base=Path(__file__).resolve().parent/'bin';root=Path(sys.argv[1]);env=os.environ.copy();assert env['WAYLAND_DISPLAY'] != env.get('REPRO_PARENT_DISPLAY')
env.update(MIRIX_PROBE_NO_REPLAY='1',MIRIX_IME_NO_CYCLE='1',MIRIX_PROBE_DISPLAY=env['WAYLAND_DISPLAY'])
os.chdir(root)
log=(root/'process.log').open('ab');results=(root/'results.jsonl').open('a',buffering=1)
def ipc(*args):return subprocess.check_output(['niri','msg',*args],env=env,text=True,timeout=15)
def shot(label):
 p=root/(label+'.png');p.unlink(missing_ok=True);ipc('action','screenshot-screen','--show-pointer','false','--path',str(p))
 for _ in range(200):
  try:
   with Image.open(p) as source:im=source.convert('RGB');im.load()
   break
  except (FileNotFoundError,OSError):time.sleep(.02)
 else:raise RuntimeError('Screenshot did not become readable')
 pts=[]
 for y in range(im.height):
  count=sum(1 for r,g,b in im.crop((0,y,im.width,y+1)).getdata() if abs(r-25)<6 and abs(g-153)<6 and abs(b-153)<6)
  if count>100:pts.append(y)
 return [min(pts),max(pts)] if pts else None
ime=subprocess.Popen([str(base/'ime-recreate')],env=env,stdout=log,stderr=log)
app=None
try:
 time.sleep(.3)
 for cycle in range(4):
  app=subprocess.Popen([str(base/'probe-cycles')],env=env,stdout=log,stderr=log)
  for _ in range(100):
   w=json.loads(ipc('--json','focused-window'))
   if w and w['pid']==app.pid:break
   time.sleep(.1)
  else:raise RuntimeError('Test client did not receive focus')
  time.sleep(1.5)
  before=shot(f'{cycle}-before')
  subprocess.run([str(base/'keys')],env=env,input='replay\n',text=True,stdout=log,stderr=log,check=True)
  time.sleep(.5);after=shot(f'{cycle}-after')
  results.write(json.dumps({'cycle':cycle,'before':before,'after':after,'mono_ns':time.monotonic_ns()})+'\n')
  app.terminate();app.wait(timeout=5);app=None;time.sleep(.3)
 results.write(json.dumps({'status':'complete'})+'\n')
except Exception as e:results.write(json.dumps({'status':'error','detail':str(e)})+'\n')
finally:
 for child in [app,ime]:
  if child is not None and child.poll() is None:
   child.terminate()
   try:child.wait(timeout=5)
   except subprocess.TimeoutExpired:child.kill();child.wait()
 (root/'done').write_text('done')
