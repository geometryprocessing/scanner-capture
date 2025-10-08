import cv2
import glob
import json
import numpy as np
import matplotlib.pyplot as plt
import os
import OpenEXR


def ensure_exists(path: str):
    '''
    Check if path to folder exists. If not, create it.
    '''
    path = os.path.dirname(path)
    if not os.path.exists(path):
        os.makedirs(path, exist_ok=True)
        
class NumpyEncoder(json.JSONEncoder):
    """ Special json encoder for numpy types """
    def default(self, obj):
        if isinstance(obj, np.integer):
            return int(obj)
        elif isinstance(obj, np.floating):
            return float(obj)
        elif isinstance(obj, np.ndarray):
            return obj.tolist()
        return json.JSONEncoder.default(self, obj)


def gen_color(dim, color=(255, 255, 255)):
    r = np.zeros((*dim, 3), dtype=np.uint8)
    r[...] = color
    return r


def gen_stripes(dim, stride, axis=0, color=(255, 255, 255)):
    img = np.zeros((*dim, 3), dtype=np.uint8)
    for i in range(0, dim[axis], 2 * stride):
        if axis == 0:
            img[i:i + stride, :, :] = color
        if axis == 1:
            img[:, i:i + stride, :] = color
    return img


def gen_dots(dim, offset=(0, 0), step=(100, 100), count=(5, 5)):
    img = np.zeros((*dim, 3), dtype=np.uint8)
    for i in range(count[0]):
        for j in range(count[1]):
            pos = np.array(offset) + np.array(step) * np.array([i, j]) + np.array([step[0] // 4, step[1] // 4])
            img[pos[0], pos[1], :] = [255, 0, 0]
            img[pos[0], pos[1] + step[1] // 2, :] = [0, 255, 0]
            img[pos[0] + step[0] // 2, pos[1], :] = [0, 0, 255]
            img[pos[0] + step[0] // 2, pos[1] + step[1] // 2, :] = [255, 255, 255]
    return img


def gen_gray(dim, color=(255, 255, 255), invert=False):
    r, c = np.indices(dim, dtype=np.uint32)
    r = np.bitwise_xor(r, np.right_shift(r, 1))
    c = np.bitwise_xor(c, np.right_shift(c, 1))

    n = np.ceil(np.log2(dim)).astype(np.uint32)
    R = np.zeros((n[0], *dim, 3), dtype=np.uint8)
    C = np.zeros((n[1], *dim, 3), dtype=np.uint8)

    for i in range(n[0]):
        idx = np.nonzero(np.bitwise_and(r, 1 << i))
        R[i, idx[0], idx[1], :] = color
        if invert:
            R[i, :, :, :] = color
            R[i, idx[0], idx[1], :] = 0

    for i in range(n[1]):
        idx = np.nonzero(np.bitwise_and(c, 1 << i))
        C[i, idx[0], idx[1], :] = color
        if invert:
            C[i, :, :, :] = color
            C[i, idx[0], idx[1], :] = 0

    return R, C


def gen_checker(dim, pos, size, count, lum=255):
    img = np.ones((*dim, 3), dtype=np.uint8) * int(lum)
    for i in range(count[0]):
        for j in range(count[1]):
            if (i + j) % 2 == 0:
                t = pos[0] + i * size
                l = pos[1] + j * size
                img[t:t+size, l:l+size, :] = 0

    return img

def adjust_patterns(path, calib_file, plot=False):
    calib = json.load(open(calib_file))
    polies = [np.array(calib[c]) for c in ["blue", "green", "red"]]

    for file in glob.glob(path + "_ref/*.png"):
        print(file)
        img = cv2.imread(file)
        # print(img)

        if plot:
            plt.figure("LUTs", (12, 7))

        for i in range(3):
            pix = np.arange(256)
            poly = polies[i]
            a, b, c = poly[0], poly[1], -pix
            lut = poly[0] * pix * pix + poly[1] * pix + poly[2]
            lut = np.round(255 * lut / lut[-1]).astype(np.uint8)
            inv_lut = (-b + np.sqrt(b*b - 4*a*c)) / (2 * a)
            inv_lut = np.round(255 * inv_lut / inv_lut[-1]).astype(np.uint8)

            img[:, :, i] = inv_lut[img[:, :, i]]

            if plot:
                print(i, inv_lut)
                plt.plot(pix, lut, label=str(i))
                plt.plot(pix, inv_lut, ".-", label="inv_"+str(i))

        if plot:
            plt.legend()
            plt.tight_layout()

            plt.show()
            break

        cv2.imwrite(path + "/" + os.path.basename(file), img)


def split_patterns(folder_in, folder_out):
    files = glob.glob(folder_in + "/*.png")
    ensure_exists(folder_out)

    for file in files:
        print(file)
        for i, c in zip([1], "g"):
        # for i, c in zip(range(3), "rgb"):
            img = cv2.imread(file)[:, :, ::-1]

            img2 = np.zeros_like(img)
            img2[:, :, i] = img[:, :, i]

            name = os.path.basename(file)
            filename = folder_out + "/" + name[:-4] + "_%s.png" % c
            filename = folder_out + "/" + name
            print(name, filename)
            cv2.imwrite(filename, img2[:, :, ::-1])

def load_openexr(filename: str,
                    make_gray: bool=False,
                    load_depth: bool=False):
    """
    Load .EXR extension image using OpenEXR. These are multi-channel,
    high-dynamic range images.

    Parameters
    ----------
    filename : str
        path to image file.  
    make_gray : boolean, optional
        Flag to convert image to grayscale (True) or keep all three color channels intact (False).
        Default is False.
    load_depth : boolean, optional
        Flag to load the channel distance.Y (True) or not (False).
        Default is False.

    Returns
    -------
    image
        numpy array (shape NxMx3 if make_gray set to False,
        shape NxMx1 if make_gray set to True) of image
    depth (optional)
        numpy array (shape NxMx1) of depth (only returned if load_depth set to True)
    """
    with OpenEXR.File(filename) as infile:
        if len(infile.header()['channels']) == 3:
            RGB = infile.channels()["RGB"].pixels
            d = None
        elif len(infile.header()['channels']) >= 4:
            R = infile.header()['channels']["R"].pixels
            G = infile.header()['channels']["G"].pixels
            B = infile.header()['channels']["B"].pixels
            RGB = np.stack([R, G, B], axis=2)

            if load_depth:
                d = infile.channel()["distance.Y"].pixels
            else:
                d = None

        img = convert_to_gray(RGB) if make_gray else RGB
        if load_depth:
            return img, d
        else:
            return img


def save_openexr(filename: str,
                    image: np.ndarray,
                    make_gray: bool=True):
    """
    Save image with .EXR extension using OpenEXR.

    Parameters
    ----------
    filename : str
        path to file where image will be saved.  
    image : array_like
        image array (if only one channel, i.e. grayscale, keeps grayscale)
    make_gray : boolean, optional
        Flag to convert RGB image to grayscale (True) or save with all three color channels (False).
        Default is True.

    Note: this function is compatible with OpenEXR 3.x, not 2.x
    """
    if len(image.shape) > 2:
        if make_gray is True:
            R = G = B = cv2.cvtColor(image, cv2.COLOR_RGB2GRAY).astype(np.float16)
        else:
            R = image[:, :, 0].astype(np.float16)
            G = image[:, :, 1].astype(np.float16)
            B = image[:, :, 2].astype(np.float16)
    else:
        R = G = B = image.astype(np.float16)

    header = {'compression': OpenEXR.PXR24_COMPRESSION}
    channels = {'R': R,
                'G': G,
                'B': B}

    with OpenEXR.File(header, channels) as outfile:
        outfile.write(filename)

def convert_to_gray(image: np.ndarray) -> np.ndarray:
    """
    Function to convert RGB image to grayscale.

    Parameters
    ----------
    image : array_like
        image array

    Returns
    ----------
    grayscale version of image
    """
    return (.2989 * image[:,:,0] \
            + .5870 * image[:,:,1] \
            + .1140 * image[:,:,2]).astype(image.dtype)