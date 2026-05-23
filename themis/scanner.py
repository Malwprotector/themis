# scanner.py
import re, csv, json, shutil, datetime, math
from dataclasses import dataclass, asdict
from pathlib import Path
from collections import Counter, defaultdict
from .lda_model import LDA

BASIC_STOPWORDS=set("a an and are as at be by de des du le la les un une et ou of on for from in into is it its no not the to with sans copy copie final new old version draft tmp temp img dsc scan document file fichier".split())
TOKEN_RE=re.compile(r"[A-Za-zÀ-ÖØ-öø-ÿ0-9]+")
HISTORY_FILE='themis_history.jsonl'

@dataclass
class FileProposal:
    selected: bool
    source: str
    destination: str
    topic: int
    topic_label: str
    confidence: float
    reason: str
    model: str = 'lda'
    category: str = ''
    bayes_label: str = ''
    bayes_confidence: float = 0.0

class MultinomialNaiveBayes:
    """Small dependency-free Multinomial Naive Bayes.
    It learns from user-approved moves stored in themis_history.jsonl.
    """
    def __init__(self, alpha=1.0):
        self.alpha=float(alpha)
        self.labels=[]
        self.label_counts=Counter()
        self.word_counts=defaultdict(Counter)
        self.total_words=Counter()
        self.vocab=set()
        self.fitted=False

    def fit(self, docs, labels):
        for doc,label in zip(docs,labels):
            label=safe_label(label)
            if not label: continue
            self.label_counts[label]+=1
            for w in doc:
                self.word_counts[label][w]+=1
                self.total_words[label]+=1
                self.vocab.add(w)
        self.labels=list(self.label_counts.keys())
        self.fitted=bool(len(self.labels) >= 2 and self.vocab)
        return self

    def predict_proba_one(self, doc):
        if not self.fitted:
            return []
        vocab_size=max(1,len(self.vocab))
        total_docs=sum(self.label_counts.values())
        scores=[]
        for label in self.labels:
            logp=math.log(self.label_counts[label]/total_docs)
            denom=self.total_words[label]+self.alpha*vocab_size
            for w in doc:
                logp+=math.log((self.word_counts[label][w]+self.alpha)/denom)
            scores.append((label,logp))
        m=max(s for _,s in scores)
        exps=[(l,math.exp(s-m)) for l,s in scores]
        z=sum(v for _,v in exps) or 1.0
        return sorted([(l,v/z) for l,v in exps], key=lambda x:x[1], reverse=True)

    def predict_one(self, doc):
        probs=self.predict_proba_one(doc)
        return probs[0] if probs else ('',0.0)

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
                if p.name == HISTORY_FILE: continue
                yield p

def unique_path(path):
    path=Path(path)
    if not path.exists(): return path
    i=1
    while True:
        c=path.with_name(f"{path.stem} ({i}){path.suffix}")
        if not c.exists(): return c
        i+=1

def safe_label(label):
    label=str(label).strip() or 'misc'
    return re.sub(r'[^A-Za-z0-9_À-ÖØ-öø-ÿ-]+','_', label).strip('_') or 'misc'

def infer_label_from_destination(destination):
    name=Path(destination).parent.name
    name=re.sub(r'^\d+[_ -]*','',name)
    return safe_label(name)

def destination_for(target, category, filename, prefix=None):
    category=safe_label(category)
    folder=f"{prefix:02d}_{category}" if prefix is not None else category
    return unique_path(Path(target)/folder/filename)

def load_history(target_root=None, roots=None):
    candidates=[]
    if target_root:
        candidates.append(Path(target_root).expanduser().resolve()/HISTORY_FILE)
    if roots:
        for r in roots:
            r=Path(r).expanduser().resolve()
            candidates.extend([r/HISTORY_FILE, r/'Themis_Sorted'/HISTORY_FILE])
    rows=[]; seen=set()
    for path in candidates:
        if path in seen or not path.exists(): continue
        seen.add(path)
        with open(path,encoding='utf-8') as f:
            for line in f:
                try: rows.append(json.loads(line))
                except Exception: pass
    return rows

def known_categories(target_root=None, roots=None):
    cats=set()
    for row in load_history(target_root, roots):
        cat=row.get('category') or row.get('bayes_label') or infer_label_from_destination(row.get('destination',''))
        if cat: cats.add(safe_label(cat))
    return sorted(cats, key=str.lower)

def train_bayes_from_history(target_root=None, roots=None, min_examples=3):
    rows=load_history(target_root, roots)
    docs=[]; labels=[]
    for r in rows:
        src=r.get('source') or r.get('destination') or ''
        dst=r.get('destination') or ''
        label=r.get('category') or r.get('bayes_label') or infer_label_from_destination(dst)
        toks=tokenize_filename(src) or tokenize_filename(dst)
        if toks and label:
            docs.append(toks); labels.append(safe_label(label))
    if len(labels) < min_examples or len(set(labels)) < 2:
        return None, len(labels), len(set(labels))
    return MultinomialNaiveBayes().fit(docs,labels), len(labels), len(set(labels))

def build_plan(roots, target_root=None, topics=8, iterations=350, recursive=True, include_hidden=False,
               min_confidence=0.0, bayes_threshold=0.68, min_bayes_examples=3):
    files=list(iter_files(roots, recursive, include_hidden))
    if not files: return []
    docs=[tokenize_filename(p) or [p.suffix.lower().lstrip('.') or 'misc'] for p in files]
    target=Path(target_root).expanduser().resolve() if target_root else Path(roots[0]).expanduser().resolve()/'Themis_Sorted'

    lda=LDA(min(max(1,int(topics)),len(files)), iterations=iterations).fit(docs)
    bayes, n_examples, n_labels = train_bayes_from_history(target, roots, min_bayes_examples)

    plan=[]
    for i,src in enumerate(files):
        topic,lda_conf=lda.dominant_topic(i)
        lda_label=safe_label(lda.topic_label(topic)) or f'topic_{topic+1}'
        chosen_category=lda_label
        chosen_model='lda'
        chosen_conf=float(lda_conf)
        bayes_label=''; bayes_conf=0.0
        reason='LDA discovered this group from filename tokens: '+', '.join(docs[i])

        if bayes:
            bayes_label,bayes_conf=bayes.predict_one(docs[i])
            if bayes_label and bayes_conf >= bayes_threshold:
                chosen_category=safe_label(bayes_label)
                chosen_model='bayes'
                chosen_conf=float(bayes_conf)
                reason=f"Bayes used approved history: {n_examples} examples, {n_labels} categories. Tokens: "+', '.join(docs[i])
            else:
                reason += f" | Bayes available but below threshold: {bayes_label} {bayes_conf:.2f}"
        else:
            reason += f" | Bayes waiting for training data: {n_examples} approved example(s), {n_labels} category/categories."

        dest=destination_for(target, chosen_category, src.name, prefix=(topic+1 if chosen_model=='lda' else None))
        plan.append(FileProposal(chosen_conf>=min_confidence, str(src), str(dest), topic+1, lda_label,
                                 round(chosen_conf,4), reason, chosen_model, chosen_category,
                                 safe_label(bayes_label) if bayes_label else '', round(float(bayes_conf),4)))
    return plan

def write_csv(plan,path):
    fields=['selected','source','destination','topic','topic_label','confidence','reason','model','category','bayes_label','bayes_confidence']
    with open(path,'w',newline='',encoding='utf-8') as f:
        wr=csv.DictWriter(f, fieldnames=fields); wr.writeheader()
        for p in plan: wr.writerow(asdict(p))

def read_csv(path):
    out=[]
    with open(path,newline='',encoding='utf-8') as f:
        for r in csv.DictReader(f):
            r['selected']=str(r.get('selected','true')).lower() in ('1','true','yes','y')
            r['topic']=int(r.get('topic') or 0); r['confidence']=float(r.get('confidence') or 0)
            r['bayes_confidence']=float(r.get('bayes_confidence') or 0)
            r.setdefault('model','lda'); r.setdefault('category', infer_label_from_destination(r.get('destination','')))
            r.setdefault('bayes_label','')
            out.append(FileProposal(**r))
    return out

def apply_plan(plan, history_root=None):
    done=[]
    for item in plan:
        if not item.selected: continue
        src=Path(item.source)
        if not src.exists(): continue
        item.category=safe_label(item.category or infer_label_from_destination(item.destination))
        dst=unique_path(Path(item.destination)); dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(str(src), str(dst))
        rec=asdict(item); rec['destination']=str(dst); rec['category']=item.category
        rec['applied_at']=datetime.datetime.now().isoformat(timespec='seconds'); done.append(rec)
    if done:
        root=Path(history_root).expanduser().resolve() if history_root else Path(done[0]['destination']).parents[1]
        root.mkdir(parents=True, exist_ok=True)
        with open(root/HISTORY_FILE,'a',encoding='utf-8') as f:
            for rec in done: f.write(json.dumps(rec,ensure_ascii=False)+chr(10))
    return done
