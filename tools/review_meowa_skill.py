"""Read-only Meowa Skill/CLI contract audit. Never submits a generation.

Default: local documentation, parser and past-job inspection only.
--refresh-catalogs calls only the six named discovery commands below.
Credentials are neither read by this script nor passed as arguments.
"""
import argparse, contextlib, hashlib, importlib.util, io, json, os, subprocess
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor

ROOT=Path(__file__).resolve().parents[1]
SKILL=Path('D:/LittleWorld-Meowa/tools/meowa-skills/skills/game-assets')
PYTHON=Path('D:/LittleWorld-Meowa/tools/venv/Scripts/python.exe')
REPORT=ROOT/'reports/visual-v4-2/skill-review'
LOG=ROOT/'reports/logs/meowa-skill-review'
DISCOVERY=[['pixel-gen-template-info'],['large-pixel-template-info'],['hd-gen-template-info'],
           ['map-reference-search','--categories'],['texture-reference-search','--categories'],['video-prompt-list']]
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def write(name,value): (REPORT/name).write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def load_runner():
    spec=importlib.util.spec_from_file_location('meowa_readonly_audit',SKILL/'meowart_api.py')
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    return module
def discover(args):
    assert args in DISCOVERY
    env=os.environ.copy();env.update(PYTHONUTF8='1',PYTHONIOENCODING='utf-8')
    p=subprocess.run([str(PYTHON),str(SKILL/'meowart_api.py'),*args],cwd='D:/LittleWorld-Meowa',env=env,
                     capture_output=True,text=True,encoding='utf-8',timeout=75)
    path=LOG/(args[0]+'.json');path.write_text(p.stdout+p.stderr,encoding='utf-8')
    return {'command':args,'exit_code':p.returncode,'sha256':sha(path)}
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--refresh-catalogs',action='store_true');opt=ap.parse_args()
    REPORT.mkdir(parents=True,exist_ok=True);LOG.mkdir(parents=True,exist_ok=True)
    runner=load_runner();parser=runner.build_parser()
    sub=next(a for a in parser._actions if isinstance(getattr(a,'choices',None),dict))
    contract=json.loads((SKILL/'web_parameter_contract.json').read_text('utf-8'));features=contract['features']
    paths=[SKILL/'SKILL.md',SKILL/'meowart_api.md',SKILL/'web_parameter_contract.json',SKILL/'meowart_api.py',*sorted((SKILL/'references').glob('*.md'))]
    sources=[{'file':p.relative_to(SKILL).as_posix(),'bytes':p.stat().st_size,'lines':len(p.read_text('utf-8').splitlines()),'sha256':sha(p)} for p in paths]
    copy=Path('C:/Users/ASUS/.codex/skills/game-assets');installed=[]
    for name in ('SKILL.md','meowart_api.py','web_parameter_contract.json'):
        p=copy/name
        installed.append({'file':name,'present':p.is_file(),'same_as_verified_runner':p.is_file() and sha(p)==sha(SKILL/name)})
    covered=[];issues=[];options_checked=0
    for name,feature in features.items():
        cmd=feature.get('command');cp=sub.choices.get(cmd);missing=[];differences=[]
        actions={flag:a for a in cp._actions for flag in a.option_strings} if cp else {}
        for param,meta in feature.get('parameters',{}).items():
            flag=meta.get('flag')
            if not flag: continue
            options_checked+=1
            if flag not in actions: missing.append(flag);continue
            a=actions[flag]
            if 'default' in meta and a.default!=meta['default']:
                differences.append({'parameter':param,'documented':meta['default'],'parser':a.default,
                    'note':'Needs conditional/model/main-entry interpretation; not automatically an API bug.'})
        if missing or cp is None: issues.append({'feature':name,'command':cmd,'missing_command':cp is None,'missing_flags':missing})
        covered.append({'feature':name,'command':cmd,'exists_in_parser':cp is not None,'parameter_flags':list(actions),
                        'missing_flags':missing,'default_differences_to_interpret':differences,
                        'generation_validated_by_this_audit':False})
    histories=[]
    for path in (ROOT/'reports/visual-v4/MEOWA_JOBS.json',ROOT/'reports/visual-v4-2/MEOWA_JOBS.json'):
        data=json.loads(path.read_text('utf-8'))
        for job in data['jobs']:
            args=job.get('command',[]);ok=False
            if args:
                with contextlib.redirect_stderr(io.StringIO()):
                    try: parser.parse_args(args);ok=True
                    except SystemExit: pass
            histories.append({'round':data['round'],'name':job['name'],'job_id':job.get('job_id'),'command':args[0] if args else None,
                'argument_parse_valid':ok,'observed_credits':job.get('observed_balance_decrease'),'acceptance':job.get('acceptance','not a generation-quality assertion')})
    checks=[]
    def rejects(name,args):
        with contextlib.redirect_stderr(io.StringIO()):
            try: parser.parse_args(args);passed=False
            except SystemExit: passed=True
        checks.append({'name':name,'rejected_by_local_parser':passed})
    rejects('strict and regional pixelation are mutually exclusive',['image-edit-run','--reference-image','input.png','--prompt','test','--strict','--regional-pixelation'])
    rejects('clone count is square-grid count, not arbitrary subject count',['asset-clone-run','--reference-image','input.png','--clone-count','2'])
    rejects('V2 frames use 8/16/24/32, not V1 12',['meowa-animation-run','--image-file','input.png','--prompt','test','--output-frames','12'])
    rejects('Pixel/HD background style uses only public modes',['meowa-animation-edit-run','--video-file','input.webp','--edit-intent','test','--primary-reference','sofa'])
    checks.append({'name':'image-led reference motion is exposed','passed':'--primary-reference' in {f for a in sub.choices['meowa-animation-edit-run']._actions for f in a.option_strings}})
    if opt.refresh_catalogs:
        with ThreadPoolExecutor(max_workers=3) as pool: discovery=list(pool.map(discover,DISCOVERY))
        write('DISCOVERY.json',discovery)
    catalogs={}
    for cmd in DISCOVERY:
        p=LOG/(cmd[0]+'.json')
        if not p.exists(): continue
        text=p.read_text('utf-8'); start=text.find('{')
        try: catalogs[cmd[0]]=json.JSONDecoder().raw_decode(text[start:])[0]
        except (ValueError,IndexError): catalogs[cmd[0]]={'unavailable':True}
    template_summary={}
    for name,data in catalogs.items():
        if 'templates' in data:
            template_summary[name]={'count':len(data['templates']),'templates':[{k:r[k] for k in ('template_name','display_name','description','output_size','output_size_px','default_count','default_resolution','default_aspect_ratio') if k in r} for r in data['templates']]}
        else: template_summary[name]=data
    write('CATALOGS.json',template_summary)
    write('CONTRACT_AUDIT.json',{'date':'2026-10-26','official_commit':subprocess.check_output(['git','-C',str(SKILL.parent.parent),'rev-parse','HEAD'],text=True).strip(),
        'sources':sources,'installed_copy':installed,'contract_feature_count':len(features),'public_help_entries':len(sub._choices_actions),
        'registered_names_including_aliases':len(sub.choices),'parameter_flags_compared':options_checked,
        'missing_contract_commands_or_flags':issues,'features':covered,'past_jobs_argument_checks':histories,'local_parser_checks':checks,
        'scope':'Documentation, actual parser, read-only catalogs and historical media only; no paid generation invoked by this audit.',
        'all_generation_capabilities_claimed_mastered':False})
    print(f'Read-only audit: {len(features)} product contract entries, {options_checked} flags, {len(checks)} local parser checks; {len(issues)} missing-command/flag groups to investigate.')
    for name,data in template_summary.items(): print(name, data.get('count','catalog loaded'))
if __name__=='__main__': main()
