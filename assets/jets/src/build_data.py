import math, json, sys
from specs import SPECS
from perf import performance
from jets import BUILDERS
def prep(a):
    a['m_combat']=a['m_empty']+0.6*a['fuel']+a['m_stores']+100*a['crew']
    AR=a['span']**2/a['S']; a['AR']=AR; a['k']=1/(math.pi*a['e']*AR)
    if 'span_swept' in a: a['k_swept']=1/(math.pi*0.75*a['span_swept']**2/a['S'])
    return a
out=[]
for a in SPECS:
    prep(a); p=performance(a)
    parts, mats = BUILDERS[a['id']]()
    pts=[v for pr in parts for ts in pr['mesh'].tris.values() for t in ts for v in t]
    a['length']=max(q[2] for q in pts)-min(q[2] for q in pts)
    a['height']=max(q[1] for q in pts)-min(q[1] for q in pts)+0.9  # + landing gear
    a['tris']=sum(pr['mesh'].count() for pr in parts)
    a['perf']={k:(round(v,3) if isinstance(v,float) else v) for k,v in p.items()}
    out.append(a)
# game stats 3..10, normalised across the fleet
def norm(key, vals, lo=3, hi=10):
    mn,mx=min(vals),max(vals)
    return [round(lo+(v-mn)/(mx-mn)*(hi-lo),1) for v in vals]
stealth={'af40':10,'sy26':8,'ks55':6,'ecf90':4.5,'lf27':3.5,'ja46':2.5,'vk33':2.5,'nf24':1.5}
fw={'SRM':1.0,'MRM':1.6,'LRM':2.2}
cols={
 'speed':[a['perf']['m_max_alt']+a['perf']['v_max_sl_kmh']/1500 for a in out],
 'turn':[a['perf']['turn_instant']+a['perf']['turn_sustained']+a['roll']/40+a['alpha_max']/15 for a in out],
 'climb':[a['perf']['climb_ms'] for a in out],
 'range':[a['perf']['range_km'] for a in out],
 'field':[-(a['perf']['takeoff_m']+a['perf']['landing_m']) for a in out],
 'firepower':[sum(fw[w[0][:3]]*w[1] for w in a['weapons'] if '(alt.)' not in w[2])+(0 if a['gun'].startswith('None') else 1.5) for a in out],
}
ns={k:norm(k,v) for k,v in cols.items()}
for i,a in enumerate(out):
    a['game']={k:ns[k][i] for k in ns}; a['game']['stealth']=stealth[a['id']]
json.dump(out, open('out/jets_data.json','w'), indent=1)
for a in out:
    p=a['perf']; print(a['id'], a['game'], 'len %.1f'%a['length'], 'Mmax',p['m_max_alt'],'range',round(p['range_km']),'LDG',round(p['landing_m']),'turnS',round(p['turn_sustained'],1))
