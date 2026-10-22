"""Hunyuan3D-2 shape stage on local weights. Usage:
 run_hunyuan.py <mv|mini> <subfolder> <out.glb> [--steps N] [--res N] [--view name=path ...] [--image path]
Records wall time and peak VRAM (torch peak allocated + driver-level free memory delta)."""
import argparse, os, sys, time, glob
import torch
from PIL import Image

ap = argparse.ArgumentParser()
ap.add_argument("kind"); ap.add_argument("subfolder"); ap.add_argument("out")
ap.add_argument("--steps", type=int, default=50); ap.add_argument("--res", type=int, default=256)
ap.add_argument("--view", action="append", default=[]); ap.add_argument("--image")
ap.add_argument("--seed", type=int, default=12345)
ap.add_argument("--guidance", type=float, default=5.0)
a = ap.parse_args()

repo = {"mv": "models--tencent--Hunyuan3D-2mv", "mini": "models--tencent--Hunyuan3D-2mini"}[a.kind]
snap = glob.glob(f"D:/mxl-3d/hf/hub/{repo}/snapshots/*")[0]
os.environ["HY3DGEN_MODELS"] = snap
sys.path.insert(0, "D:/mxl-3d/Hunyuan3D-2")
from hy3dgen.shapegen import Hunyuan3DDiTFlowMatchingPipeline

free0, total = torch.cuda.mem_get_info()
t0 = time.time()
pipe = Hunyuan3DDiTFlowMatchingPipeline.from_pretrained(".", subfolder=a.subfolder, variant="fp16")
torch.cuda.synchronize(); tload = time.time() - t0
if a.view:
    image = {k: Image.open(p).convert("RGBA") for k, p in (v.split("=", 1) for v in a.view)}
else:
    image = Image.open(a.image).convert("RGBA")
t1 = time.time()
mesh = pipe(image=image, num_inference_steps=a.steps, octree_resolution=a.res, num_chunks=20000,
            guidance_scale=a.guidance, generator=torch.manual_seed(a.seed), output_type="trimesh")[0]
torch.cuda.synchronize(); tgen = time.time() - t1
free1, _ = torch.cuda.mem_get_info()
mesh.export(a.out)
print(f"RESULT kind={a.kind}/{a.subfolder} views={len(a.view) or 1} steps={a.steps} res={a.res} load={tload:.1f}s gen={tgen:.1f}s "
      f"peak_alloc={torch.cuda.max_memory_allocated()/1e9:.2f}GB peak_reserved={torch.cuda.max_memory_reserved()/1e9:.2f}GB "
      f"verts={len(mesh.vertices)} faces={len(mesh.faces)} out={a.out}")
