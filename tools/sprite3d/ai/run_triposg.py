"""TripoSG on local weights, no diso, no RMBG (inputs are RGBA). Usage: run_triposg.py <in.png> <out.glb> [--steps N]"""
import argparse, glob, sys, time
sys.path.insert(0, "D:/mxl-3d/scripts/shims")
sys.path.insert(0, "D:/mxl-3d/TripoSG"); sys.path.insert(0, "D:/mxl-3d/TripoSG/scripts")
import numpy as np, torch, trimesh
from triposg.pipelines.pipeline_triposg import TripoSGPipeline
from image_process import prepare_image

ap = argparse.ArgumentParser(); ap.add_argument("img"); ap.add_argument("out")
ap.add_argument("--steps", type=int, default=50); ap.add_argument("--guidance", type=float, default=7.0)
ap.add_argument("--seed", type=int, default=42); ap.add_argument("--dense", type=int, default=8)
a = ap.parse_args()
w = glob.glob("D:/mxl-3d/hf/hub/models--VAST-AI--TripoSG/snapshots/*")[0]
t0 = time.time()
pipe = TripoSGPipeline.from_pretrained(w).to("cuda", torch.float16)
torch.cuda.synchronize(); tload = time.time() - t0
img = prepare_image(a.img, bg_color=np.array([1.0, 1.0, 1.0]), rmbg_net=None)
t1 = time.time()
with torch.no_grad():
    out = pipe(image=img, generator=torch.Generator(device="cuda").manual_seed(a.seed), num_inference_steps=a.steps,
               guidance_scale=a.guidance, use_flash_decoder=False, dense_octree_depth=a.dense).samples[0]
torch.cuda.synchronize(); tgen = time.time() - t1
mesh = trimesh.Trimesh(out[0].astype(np.float32), np.ascontiguousarray(out[1]))
mesh.export(a.out)
print(f"RESULT triposg steps={a.steps} load={tload:.1f}s gen={tgen:.1f}s peak_alloc={torch.cuda.max_memory_allocated()/1e9:.2f}GB "
      f"peak_reserved={torch.cuda.max_memory_reserved()/1e9:.2f}GB verts={len(mesh.vertices)} faces={len(mesh.faces)} out={a.out}")
