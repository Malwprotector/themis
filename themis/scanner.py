#scan
import re, csv, json, shutil, datetime
from dataclasses import dataclass, asdict
from pathlib import Path
from .lda_model import LDA

BASIC_STOPWORDS=set("a an and are as at be by de des du le la les un une et ou of on for from in into is it its no not the to with sans copy copie final new old version draft tmp temp img dsc scan document file fichier".split())
TOKEN_RE=re.compile(r"[A-Za-zÀ-ÖØ-öø-ÿ0-9]+")

@dataclass
class FileProposal:
    selected: bool
    source: str
    destination: str
    topic: int
    topic_label: str
    confidence: float
    reason: str

def stopwords():
    try:
        import nltk
        return set(nltk.corpus.stopwords.words('english')) | set(nltk.corpus.stopwords.words('french')) | BASIC_STOPWORDS
    except Exception:
        return BASIC_STOPWORDS

def tokenize_filename(path):
    stem=Path(path).stem.replace('_',' ').replace('-',' ').replace('.',' ')
    sw=stopwords()
    return [w.lower() for w in TOKEN_RE.findall(stem) if len(w)>1 and w.lower() not in sw]

def iter_files(roots, recursive=True, include_hidden=False):
    for root in roots:
        root=Path(root).expanduser().resolve()
        if not root.exists(): continue
        for p in (root.rglob('*') if recursive else root.glob('*')):
            if p.is_file() and (include_hidden or not any(part.startswith('.') for part in p.parts)):
                yield p

def unique_path(path):
    path=Path(path)
    if not path.exists(): return path
    i=1
    while True:
        c=path.with_name(f"{path.stem} ({i}){path.suffix}")
        if not c.exists(): return c
        i+=1

def build_plan(roots, target_root=None, topics=8, iterations=350, recursive=True, include_hidden=False, min_confidence=0.0):
    files=list(iter_files(roots, recursive, include_hidden))
    if not files: return []
    docs=[tokenize_filename(p) or [p.suffix.lower().lstrip('.') or 'misc'] for p in files]
    lda=LDA(min(max(1,int(topics)),len(files)), iterations=iterations).fit(docs)
    target=Path(target_root).expanduser().resolve() if target_root else Path(roots[0]).expanduser().resolve()/'Themis_Sorted'
    plan=[]
    for i,src in enumerate(files):
        topic,conf=lda.dominant_topic(i)
        label=re.sub(r'[^A-Za-z0-9_À-ÖØ-öø-ÿ-]+','_', lda.topic_label(topic)) or f'topic_{topic+1}'
        dest=unique_path(target/f"{topic+1:02d}_{label}"/src.name)
        plan.append(FileProposal(conf>=min_confidence, str(src), str(dest), topic+1, label, round(float(conf),4), 'dominant topic from filename tokens: '+', '.join(docs[i])))
    return plan

def write_csv(plan,path):
    fields=['selected','source','destination','topic','topic_label','confidence','reason']
    with open(path,'w',newline='',encoding='utf-8') as f:
        wr=csv.DictWriter(f, fieldnames=fields); wr.writeheader()
        for p in plan: wr.writerow(asdict(p))

def read_csv(path):
    out=[]
    with open(path,newline='',encoding='utf-8') as f:
        for r in csv.DictReader(f):
            r['selected']=str(r.get('selected','true')).lower() in ('1','true','yes','y')
            r['topic']=int(r.get('topic') or 0); r['confidence']=float(r.get('confidence') or 0)
            out.append(FileProposal(**r))
    return out

def apply_plan(plan, history_root=None):
    done=[]
    for item in plan:
        if not item.selected: continue
        src=Path(item.source)
        if not src.exists(): continue
        dst=unique_path(Path(item.destination)); dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(str(src), str(dst))
        rec=asdict(item); rec['destination']=str(dst); rec['applied_at']=datetime.datetime.now().isoformat(timespec='seconds'); done.append(rec)
    if done:
        root=Path(history_root).expanduser().resolve() if history_root else Path(done[0]['destination']).parents[1]
        with open(root/'themis_history.jsonl','a',encoding='utf-8') as f:
            for rec in done: f.write(json.dumps(rec,ensure_ascii=False)+chr(10))
    return done
