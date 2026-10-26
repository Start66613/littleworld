"""Budget journal around the unmodified official CLI; never accepts credentials."""
import argparse, datetime, json, re, subprocess, sys, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PYTHON = Path('D:/LittleWorld-Meowa/tools/venv/Scripts/python.exe')
CLI = Path('D:/LittleWorld-Meowa/tools/meowa-skills/skills/game-assets/meowart_api.py')
AUTH_CWD = Path('D:/LittleWorld-Meowa')
LEDGER = ROOT/'reports/visual-v4-2/MEOWA_JOBS.json'

def balance():
    # Only read-only checks retry; a billable submission is never auto-retried.
    for attempt in range(3):
        p = subprocess.run([str(PYTHON), str(CLI), 'credits-balance'], cwd=AUTH_CWD, capture_output=True, text=True, encoding='utf-8')
        if p.returncode == 0:
            start=p.stdout.find('{')
            if start>=0: return json.loads(p.stdout[start:])['total_credits']
        print(f'Read-only balance unavailable ({attempt+1}/3); no paid submission.',flush=True)
        if attempt<2: time.sleep(1)
    raise RuntimeError('Official balance unavailable; paid submission blocked')

def save(data):
    data['actual_credits_spent']=sum(max(0,j.get('observed_balance_decrease',0)) for j in data['jobs'])
    data['remaining_round_credits']=data['approved_limit']-data['actual_credits_spent']
    data['production_state']='V4.2 production resumed by user; serial submission, original-job recovery, visual review required.'
    temp = LEDGER.with_suffix('.tmp')
    temp.write_text(json.dumps(data, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    temp.replace(LEDGER)

def main():
    global LEDGER
    p=argparse.ArgumentParser(); p.add_argument('--name',required=True); p.add_argument('--ledger',type=Path,default=LEDGER); p.add_argument('--max-credits',type=int,required=True); p.add_argument('--recover',action='store_true'); p.add_argument('command',nargs=argparse.REMAINDER); a=p.parse_args();LEDGER=a.ledger
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
        approved=min(data['authorized_limit'],data.get('approved_limit',0))
        if a.max_credits<=0 or reserved+a.max_credits>approved: raise ValueError('Explicitly approved batch budget is not available; no submission')
        row={'name':a.name,'reserved_credits':a.max_credits,'command':command,'started':datetime.datetime.now().astimezone().isoformat(),'balance_before':balance(),'status':'submitting','job_id':None,'acceptance':'pending visual inspection'}
        data['jobs'].append(row); save(data)
    log=ROOT/'reports/logs'/('v42-meowa-'+a.name+('-recover' if a.recover else '')+'.log'); log.parent.mkdir(parents=True,exist_ok=True)
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
        after=balance()
        if a.recover and 'observed_balance_decrease' in row:
            row['balance_after_recovery']=after
        else:
            row['balance_after']=after; row['observed_balance_decrease']=row['balance_before']-after
    finally: save(data)
    return code

if __name__=='__main__': sys.exit(main())
