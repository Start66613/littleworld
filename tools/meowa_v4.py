"""Budget journal around the unmodified official CLI; never accepts credentials."""
import argparse, datetime, json, re, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PYTHON = Path('D:/LittleWorld-Meowa/tools/venv/Scripts/python.exe')
CLI = Path('D:/LittleWorld-Meowa/tools/meowa-skills/skills/game-assets/meowart_api.py')
AUTH_CWD = Path('D:/LittleWorld-Meowa')
LEDGER = ROOT/'reports/visual-v4/MEOWA_JOBS.json'

def balance():
    p = subprocess.run([str(PYTHON), str(CLI), 'credits-balance'], cwd=AUTH_CWD, capture_output=True, text=True, encoding='utf-8')
    if p.returncode: raise RuntimeError('Official balance query failed; no paid request permitted')
    return json.loads(p.stdout)['total_credits']

def save(data):
    temp = LEDGER.with_suffix('.tmp')
    temp.write_text(json.dumps(data, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    temp.replace(LEDGER)

def main():
    p=argparse.ArgumentParser(); p.add_argument('--name',required=True); p.add_argument('--max-credits',type=int,required=True); p.add_argument('--recover',action='store_true'); p.add_argument('command',nargs=argparse.REMAINDER); a=p.parse_args()
    command=a.command; command=command[1:] if command and command[0]=='--' else command
    if not command or any('key' in x.lower() and x.startswith('--') for x in command): raise ValueError('Official command required; credential arguments prohibited')
    data=json.loads(LEDGER.read_text(encoding='utf-8'))
    prior=next((j for j in data['jobs'] if j['name']==a.name),None)
    if a.recover:
        if not prior or not prior.get('job_id') or not ('poll' in command[0] and prior['job_id'] in command): raise ValueError('Recovery must poll the original recorded Job')
        row=prior
    else:
        if prior: raise ValueError('Name already submitted; recover its Job instead')
        reserved=sum(j['reserved_credits'] for j in data['jobs'])
        if a.max_credits<=0 or reserved+a.max_credits>data['authorized_limit']: raise ValueError('1000-credit hard budget reached')
        row={'name':a.name,'reserved_credits':a.max_credits,'command':command,'started':datetime.datetime.now().astimezone().isoformat(),'balance_before':balance(),'status':'submitting','job_id':None,'acceptance':'pending visual inspection'}
        data['jobs'].append(row); save(data)
    log=ROOT/'reports/logs'/('v4-meowa-'+a.name+('-recover' if a.recover else '')+'.log'); log.parent.mkdir(parents=True,exist_ok=True)
    env=__import__('os').environ.copy(); env['PYTHONIOENCODING']='utf-8'; env['PYTHONUNBUFFERED']='1'
    process=subprocess.Popen([str(PYTHON),str(CLI),*command],cwd=AUTH_CWD,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace',env=env)
    last_status=''; captured=[]
    with log.open('w',encoding='utf-8') as out:
        for line in process.stdout:
            safe=re.sub(r'ma_live_[A-Za-z0-9_-]+','[REDACTED]',line)
            safe=re.sub(r'https://\S+','[media URL omitted]',safe)
            out.write(safe); out.flush()
            captured.append(safe)
            match=re.search(r'(?:api_)?job_id=(\S+)',safe)
            if match: row['job_id']=match.group(1); row['status']='accepted'; save(data)
            if safe.startswith('[ERROR]') or 'submitted api_job_id=' in safe or 'saved_dir=' in safe or (safe.startswith('[INFO] status=') and safe!=last_status):
                print(safe,end='',flush=True); last_status=safe
    code=process.wait(); row['exit_code']=code; row['status']='downloaded' if code==0 else 'recover-original-job'; row['finished']=datetime.datetime.now().astimezone().isoformat()
    for match in re.finditer(r'"credit_cost"\s*:\s*(\d+)', ''.join(captured)):
        row['official_credit_cost']=int(match.group(1))
    try:
        row['balance_after']=balance(); row['observed_balance_decrease']=row['balance_before']-row['balance_after']
    finally: save(data)
    return code

if __name__=='__main__': sys.exit(main())
