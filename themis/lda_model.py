#IDA
import random
from collections import Counter

class LDA:
    """Lightweight Gibbs-sampling LDA adapted for file-name tokens."""
    def __init__(self, topics=8, iterations=350, alpha=0.1, beta=0.1, seed=0):
        self.K=max(1,int(topics)); self.iterations=max(1,int(iterations))
        self.alpha=float(alpha); self.beta=float(beta); self.random=random.Random(seed)
    def _sample(self, weights):
        total=sum(weights)
        if total<=0: return self.random.randrange(self.K)
        r=total*self.random.random()
        for i,w in enumerate(weights):
            r-=w
            if r<=0: return i
        return len(weights)-1
    def _p_t_d(self,t,d):
        return (self.document_topic_counts[d][t]+self.alpha)/(self.document_lengths[d]+self.K*self.alpha)
    def _p_w_t(self,w,t):
        return (self.topic_word_counts[t][w]+self.beta)/(self.topic_counts[t]+self.W*self.beta)
    def fit(self, documents):
        self.D=len(documents); self.W=max(1,len({w for doc in documents for w in doc}))
        self.document_topic_counts=[Counter() for _ in documents]
        self.topic_word_counts=[Counter() for _ in range(self.K)]
        self.topic_counts=[0 for _ in range(self.K)]
        self.document_lengths=[len(d) for d in documents]
        assign=[[self.random.randrange(self.K) for _ in doc] for doc in documents]
        for d,doc in enumerate(documents):
            for w,t in zip(doc,assign[d]):
                self.document_topic_counts[d][t]+=1; self.topic_word_counts[t][w]+=1; self.topic_counts[t]+=1
        for _ in range(self.iterations):
            for d,doc in enumerate(documents):
                for i,w in enumerate(doc):
                    t=assign[d][i]
                    self.document_topic_counts[d][t]-=1; self.topic_word_counts[t][w]-=1; self.topic_counts[t]-=1; self.document_lengths[d]-=1
                    nt=self._sample([self._p_w_t(w,k)*self._p_t_d(k,d) for k in range(self.K)])
                    assign[d][i]=nt
                    self.document_topic_counts[d][nt]+=1; self.topic_word_counts[nt][w]+=1; self.topic_counts[nt]+=1; self.document_lengths[d]+=1
        return self
    def document_distribution(self,d):
        total=sum(self.document_topic_counts[d].values())+self.K*self.alpha
        return [(self.document_topic_counts[d][k]+self.alpha)/total for k in range(self.K)]
    def dominant_topic(self,d):
        dist=self.document_distribution(d); k=max(range(self.K), key=lambda x: dist[x]); return k, dist[k]
    def topic_label(self,k,topn=3):
        words=[w for w,c in self.topic_word_counts[k].most_common(topn) if c>0]
        return '_'.join(words) if words else f'topic_{k+1}'
