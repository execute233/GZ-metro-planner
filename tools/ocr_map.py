"""Offline OCR candidates in source-image coordinates; never runtime code."""

import json
from pathlib import Path

import numpy as np
from PIL import Image
from rapidocr_onnxruntime import RapidOCR

root = Path(__file__).resolve().parents[1]
im = Image.open(root / "data/railway.png").convert("RGB")
ocr = RapidOCR(intra_op_num_threads=4, inter_op_num_threads=2)
out = []
for y in range(0, im.height, 900):
    for x in range(0, im.width, 900):
        crop = im.crop((x, y, min(x + 1100, im.width), min(y + 1100, im.height)))
        result, _ = ocr(np.array(crop.resize((crop.width * 2, crop.height * 2))))
        for box, word, score in result or []:
            if any("\u4e00" <= c <= "\u9fff" for c in word):
                out.append(
                    {
                        "text": word,
                        "score": score,
                        "box": [[a / 2 + x, b / 2 + y] for a, b in box],
                    }
                )
        print(x, y, len(out), flush=True)
(root / "data/source/ocr.json").write_text(
    json.dumps(out, ensure_ascii=False, indent=2), encoding="utf8"
)
