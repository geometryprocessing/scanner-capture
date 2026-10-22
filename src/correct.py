import os
import queue
import threading
import joblib
import time
from concurrent import futures

import cv2
from scipy.ndimage import gaussian_filter

from .utils import *

wb_1 = (1.0, 1.0, 1.0)


def combine_colors(prefix, black, suffixes=("_b", "_g", "_r"), wb=wb_1, sigma=1.0, subtract_blank=False):
    bgr = []

    for c, b in zip(suffixes, wb):
        d = cv2.imread(prefix + c + ".tiff", cv2.IMREAD_UNCHANGED).astype(np.float32)

        if sigma is not None:
            d = gaussian_filter(d, sigma=sigma)

        # Off by default: the blank (black level) is subtracted later in the lookup calibration code
        if subtract_blank:
            d = np.maximum(0.0, d - black) #* b

        bgr.append(d.astype(np.uint16))

    cv2.imwrite(prefix + ".tiff", np.stack(bgr, axis=2))


class Corrector:
    def __init__(self, subtract_blank=False):
        self.subtract_blank = subtract_blank
        self.async_executor = futures.ThreadPoolExecutor(max_workers=8)
        self.async_executor2 = futures.ThreadPoolExecutor(max_workers=2)
        self.nonce, self.tail = 0, 0
        self.queue = queue.Queue()
        self.dark_frames = {}
        self.failed = False
        self.e = None

    def check_errors(self, future):
        if self.e is not None and not self.failed:
            self.failed = True
            raise self.e

    def correct_and_save(self, img, exp, filename):
        if self.failed:
            raise RuntimeError("Cannot proceed after error!")

        self.nonce += 1
        future = self.async_executor.submit(self._correct_and_save, img, exp, filename)
        future.add_done_callback(self.check_errors)

    def _correct_and_save(self, img, exp, filename):
        try:
            dark_frame = self.dark_frames[str(exp)]
            # print(np.min(dark_frame), np.max(dark_frame))
            img = np.maximum(0, img.astype(np.float32) - dark_frame)
            r, c = np.nonzero(dark_frame > 2 ** 8)
            # Clamp neighbours so hot pixels on the border don't index out of bounds
            h, w = img.shape[:2]
            c0, c1 = np.clip(c - 1, 0, w - 1), np.clip(c + 1, 0, w - 1)
            r0, r1 = np.clip(r - 1, 0, h - 1), np.clip(r + 1, 0, h - 1)
            for i in range(10):
                img[r, c] = 0.25 * (img[r, c1] + img[r, c0] + img[r1, c] + img[r0, c])
            # print("Replaced %d hot pixel(s) in" % r.shape[0], filename)
            img /= 2 ** 12 - 3  # Normalize by 4093 (4094 - dark floor)
            assert filename.endswith(".tiff")
            # Saturated pixels exceed 1 after normalization, clip to avoid uint16 wrap-around
            cv2.imwrite(filename, np.round(np.clip(img, 0, 1) * (2 ** 16 - 1)).astype(np.uint16))
            print("Saved", filename)
        except Exception as e:
            print(e)
            self.e = e
        self.tail += 1

    def process(self, path, blank_name):
        if self.failed:
            raise RuntimeError("Cannot proceed after error!")

        future = self.async_executor.submit(self._process, path, blank_name, int(self.nonce))
        future.add_done_callback(self.check_errors)

    def _process(self, path, blank_name, nonce):
        while self.tail < nonce:
            time.sleep(0.1)

        try:
            print("Processing", path)
            blank = None
            if self.subtract_blank:
                blank = cv2.imread(path + blank_name, cv2.IMREAD_UNCHANGED).astype(np.float32)
                blank = gaussian_filter(blank, sigma=1.5)
                blank /= (2.5 / 0.25)

            # m = np.sort(blank.ravel())[int(0.995 * blank.size)]
            # blank2 = np.power(blank / m, 0.75)
            # save_ldr(path + blank_name[:blank_name.rfind(".")] + ".jpg",
            #          np.minimum(255 * blank2, 255).astype(np.uint8), ensure_rgb=True)

            # checker = cv2.imread(path + "checker.tiff", cv2.IMREAD_UNCHANGED).astype(np.float32)
            # uniform = cv2.imread(path + "green.tiff", cv2.IMREAD_UNCHANGED).astype(np.float32)
            # uniform = gaussian_filter(uniform, sigma=1.5)
            #
            # checker = np.maximum(0.0, checker - blank)
            # uniform = np.maximum(0.0, uniform - blank)
            # checker = checker / uniform
            # checker[uniform < 0.01 * np.max(uniform)] = 0
            # checker = np.power(checker / 1.1, 0.75)
            #
            # save_ldr(path + "checker.jpg", np.minimum(255 * checker, 255).astype(np.uint8), ensure_rgb=True)

            jobs = [joblib.delayed(combine_colors)(file[:-7], blank, sigma=1.5,
                                                  subtract_blank=self.subtract_blank) for file in glob.glob(path + "*_r.tiff")]
            joblib.Parallel(verbose=15, n_jobs=len(jobs), batch_size=len(jobs), pre_dispatch="all", backend="threading")(jobs)

            for c in ["r", "g", "b"]:
                [os.remove(file) for file in glob.glob(path + "*_%s.tiff" % c)]

            print("Processed", path)
        except Exception as e:
            print(e)
            self.e = e

