import json
import os.path
import numpy as np
import cv2

from .utils import NumpyEncoder

def ensure_exists(path):
    path = os.path.dirname(path)
    if not os.path.exists(path):
        os.makedirs(path, exist_ok=True)


def debundle(bundle_data, bundle_meta, file_id=None):
    n_buffers = bundle_meta["n_buffers"]

    data = [bundle_data[i, ...] for i in range(n_buffers)]
    metas = [bundle_meta["buffer_%d" % i] for i in range(n_buffers)]

    for i in  range(n_buffers):
        metas[i]["bundle_id"] = bundle_meta["bundle_id"]
        metas[i]["buffer_id"] = i
        if file_id is not None:
            metas[i]["file_id"] = file_id

    return data, metas

class NumpyWriter:
    debug = False  # Print debug if True
    filename_template = "%d"  # Filename template with integer %d string at 0 by default
    file_id = 0  # Starting & current file ID

    def init(self):
        ensure_exists(self.filename_template % 0)

    def process(self, *xs, meta=None, file_id):
        assert(len(xs) == 1)
        if xs[0] is not None:
            assert(type(xs[0]) == np.ndarray)

        if xs[0] is None and meta is None:
            return None

        # filename = self.filename_template % self.file_id # original piece of code
        filename = self.filename_template % file_id

        if self.debug:
            print("Numpy_Writer filename:", filename, xs[0].shape if xs[0] is not None else None, meta)

        if xs[0] is not None:
            with open(filename + ".npy", "wb") as f:
                np.save(f, xs[0])
        
        # if meta is not None:
        #     meta["file_id"] = self.file_id

        #     with open(filename + ".json", "w") as f:
        #         json.dump(dict(meta), f, indent=4, cls=NumpyEncoder)

        # self.file_id += 1 # original piece of code

        return [filename], meta


class NumpyReader:
    debug = False  # Print debug if True
    verbose = False  # Print verbose debug if True
    mmap = False  # TODO: mmap if True instead or reading and making a copy
    filename_template="%d"  # Filename template with integer %d string at 0 by default
    file_id = 0  # Starting & current file ID

    def process(self, *xs, meta=None):
        filename = self.filename_template % self.file_id

        if os.path.exists(filename + ".npy"):
            with open(filename + ".npy", "rb") as f:
                # data = np.load(f, mmap=self.mmap)  # Not supported
                data = np.load(f)
        else:
            data = None

        if os.path.exists(filename + ".json"):
            with open(filename + ".json", "r") as f:
                meta = json.load(f)
                meta["file_id"] = self.file_id
        else:
            meta = {}

        self.file_id += 1

        if data is None and meta is None:
            if self.debug and self.verbose:
                print("Numpy_Reader missing:", filename)
            return None
        else:
            if self.debug:
                print("Numpy_Reader filename:", filename)
            return [data], meta


if __name__ == '__main__':
    # w = NumpyWriter(filename_template="test_data/%d", debug=True, verbose=True)

    # print(w.process(np.zeros((1000, 1000)), meta={"meta": "test"}))
    # print(w.process(None, meta={"meta": "test"}))

    r = NumpyReader()
    r.filename_template = "E:/images/%d"
    r.verbose = True
    r.debug = True

    import matplotlib.pyplot as plt

    for i in range(10):
        bundle_data, bundle_meta = r.process([])
        data, metas = debundle(bundle_data[0], bundle_meta, file_id=i)

        for j, (img, meta) in enumerate(zip(data, metas)):
            print(i, j, img.shape, img.dtype, meta)
            img = cv2.cvtColor(img/255, cv2.COLOR_BayerRGGB2BGR)
            # plt.imshow(img)
            # plt.show()
            cv2.imwrite(r.filename_template % meta["image_number"] + ".png", (img[:, :, ::-1] * 255).astype(np.uint8))
