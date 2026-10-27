"""Throwaway: a transparent render laid over the right of a 16:9 crop of a game screenshot."""
import bpy
import numpy as np


def composite(render_path, bg_path, out_path, W=1600, H=900, x0=560):
    src = bpy.data.images.load(bg_path)
    sw, sh = src.size
    px = np.array(src.pixels[:], dtype=np.float32).reshape(sh, sw, 4)[::-1]
    cw = sw - x0
    ch = int(cw * H / W)
    crop = px[0:ch, x0:x0 + cw]
    ys, xs = np.linspace(0, ch - 1, H), np.linspace(0, cw - 1, W)
    y0i, x0i = np.floor(ys).astype(int), np.floor(xs).astype(int)
    y1i, x1i = np.minimum(y0i + 1, ch - 1), np.minimum(x0i + 1, cw - 1)
    fy, fx = (ys - y0i)[:, None, None], (xs - x0i)[None, :, None]
    top = crop[y0i][:, x0i] * (1 - fx) + crop[y0i][:, x1i] * fx
    bot = crop[y1i][:, x0i] * (1 - fx) + crop[y1i][:, x1i] * fx
    back = top * (1 - fy) + bot * fy
    fg_img = bpy.data.images.load(render_path)
    fg = np.array(fg_img.pixels[:], dtype=np.float32).reshape(H, W, 4)[::-1]
    a = fg[:, :, 3:4]
    outpx = fg[:, :, :3] * a + back[:, :, :3] * (1 - a)
    result = bpy.data.images.new("comp", W, H)
    result.pixels[:] = np.concatenate([outpx, np.ones((H, W, 1), np.float32)], axis=2)[::-1].ravel()
    result.filepath_raw = out_path
    result.file_format = "PNG"
    result.save()
