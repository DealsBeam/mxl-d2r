# Stand-in for the CUDA-compiled `diso` package (DiffDMC). TripoSG only uses it in the flash decoder path;
# we run with use_flash_decoder=False, which falls back to skimage marching cubes.
class DiffDMC:
    def __init__(self, *a, **k):
        raise RuntimeError("diso (CUDA extension) is not installed; run with use_flash_decoder=False")
