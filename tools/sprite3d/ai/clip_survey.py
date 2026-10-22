import sys, json, glob, os
sys.path.insert(0, r'%USERPROFILE%\.t3\worktrees\mxl-d2r\t3-7128fd6d\tools')
import d2r_model as dm
root = 'D:/mxl-3d/anims/data/hd/character/enemy/'
res = {}
for u in sorted(os.listdir(root)):
    a = f'{root}{u}/animation/combined.animations'; s = glob.glob(f'{root}{u}/skeleton/*.skeleton')
    if not os.path.exists(a): continue
    try:
        an = dm.parse_tree(dm.Gr2File.read(open(a,'rb').read()))
        clips = {x['Name']: round(float(x['Duration']), 2) for x in an['Animations']}
        nb = len(dm.parse_tree(dm.Gr2File.read(open(s[0],'rb').read()))['Skeletons'][0]['Bones']) if s else None
        res[u] = {'clips': clips, 'bones': nb}
    except Exception as e:
        res[u] = {'error': str(e)}
json.dump(res, open('D:/mxl-3d/out/wereowl/clips.json','w'), indent=1)
print(len(res))
