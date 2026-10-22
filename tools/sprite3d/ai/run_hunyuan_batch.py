"""Hunyuan3D-2mv, one model load, many jobs: run_hunyuan_batch.py <outdir> <steps> <res> <seed,seed..> <name=front,left,back,right dirs...>
dirs are sprite direction numbers; images from views_x4 (nu_d<N>.png)."""
import glob, os, sys, time
import torch
from PIL import Image
outdir, steps, res, seeds = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), [int(s) for s in sys.argv[4].split(",")]
jobs = [j.split("=") for j in sys.argv[5:]]
os.environ["HY3DGEN_MODELS"] = glob.glob("D:/mxl-3d/hf/hub/models--tencent--Hunyuan3D-2mv/snapshots/*")[0]
sys.path.insert(0, "D:/mxl-3d/Hunyuan3D-2")
from hy3dgen.shapegen import Hunyuan3DDiTFlowMatchingPipeline
pipe = Hunyuan3DDiTFlowMatchingPipeline.from_pretrained(".", subfolder="hunyuan3d-dit-v2-mv", variant="fp16")
os.makedirs(outdir, exist_ok=True)
for name, dirs in jobs:
    ds = dirs.split(",")
    vd = os.environ.get("VIEWDIR", "D:/mxl-3d/out/wereowl/views_x4")
    img = {k: Image.open(f"{vd}/nu_d{d}.png").convert("RGBA") for k, d in zip(["front", "left", "back", "right"], ds)}
    for seed in seeds:
        out = f"{outdir}/{name}_s{seed}.glb"
        if os.path.exists(out): continue
        t = time.time()
        mesh = pipe(image=img, num_inference_steps=steps, octree_resolution=res, num_chunks=20000, guidance_scale=5.0,
                    generator=torch.manual_seed(seed), output_type="trimesh")[0]
        mesh.export(out)
        print(f"RESULT {name} seed={seed} dirs={dirs} gen={time.time()-t:.1f}s peak={torch.cuda.max_memory_allocated()/1e9:.2f}GB verts={len(mesh.vertices)}", flush=True)
