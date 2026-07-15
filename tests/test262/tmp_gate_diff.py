import pickle, json
base = pickle.load(open('.gate_base.pkl', 'rb'))
# LAST occurrence wins: tmp_gate_res.jsonl may contain appended lines from
# earlier runs/sessions; counting "any pass" masked real losses (2026-07-15 —
# master was LOST:4 while the diff reported 0). Best practice is still to
# DELETE the results file before each gate run.
status = {}
for line in open('tmp_gate_res.jsonl', encoding='utf-8'):
    try:
        r = json.loads(line)
    except Exception:
        continue
    p = r.get('path') or r.get('test') or r.get('file')
    st = (r.get('status') or r.get('result') or '').lower()
    if p:
        status[p.replace('/', '\\')] = st
now = {p for p, st in status.items() if 'pass' in st}
lost = sorted(base - now)
gained = sorted(now - base)
print("baseline:", len(base), "now:", len(now))
print("LOST:", len(lost), " GAINED:", len(gained))
nonTA = [x for x in lost if 'TypedArray' not in x and 'BigInt' not in x]
print("LOST non-TypedArray:", len(nonTA))
for x in nonTA[:40]:
    print("  -", x)
