# sprite -> 3D prototype helpers (lane `wereowl-3d`)

Prototype: turn a Median 2D monster sprite into a D2R HD model. Everything heavy (Blender, Real-ESRGAN, the 3D venv, model
weights, meshes) lives under `D:\mxl-3d\` and is never committed.

| Script | Use |
|---|---|
| `dcc_frames.py` | Median/D2R DCC -> RGBA PNG frames (all 16 directions, palette from D2R `pal.dat`) |
| `texture_png.py` | D2R `.texture` (BC1/BC3/RGBA8) -> PNG through Pillow; format 62 is BC3 (not BC7) |
| `model_to_glb.py` | D2R `.model` -> GLB (bind pose + albedo), runs in the 3D venv (needs trimesh) |
| `render_glb.py` | `blender -b -P` 4-angle Workbench render strip of GLBs |
| `donor_info.py`, `extract_anims.py`, `ai/clip_survey.py` | skeleton bone counts and real clip names of the D2R enemy units |
| `ai/run_hunyuan.py`, `ai/run_triposg.py`, `ai/diso_stub.py` | Hunyuan3D-2 (mv / mini) and TripoSG shape generation on local weights (copies of what ran from `D:\mxl-3d\scripts`) |

Environment notes (RX 9070 XT, Windows, build of 2026-10-22):
- Python 3.12 venv, AMD wheels from `repo.radeon.com/rocm/windows/rocm-rel-7.2.1/` (torch 2.9.1+rocm7.2.1); works with the installed
  driver 32.0.31041.1004, `torch.cuda.is_available()` is True.
- Hunyuan3D-2 needs `transformers<5` (5.x renamed the DINOv2 attention weights and the checkpoint no longer loads) and
  `diffusers<0.36`.
- TripoSG imports `diso` (CUDA extension). `ai/diso_stub.py` is a stand-in; run with `use_flash_decoder=False` (skimage marching cubes).
- Not installed: the Hunyuan texture stage (CUDA rasterizer), `diso`, `rembg`, `gradio`.
