### PLEASE CHANGE THE PARAMETERS HERE BEFORE RUNNING
DEVICE_CONFIGS = [
    {            
        'skip': False,
        'maker': 'Triton',
        'serial_number': '253102176',
        'model': 'TRX051S-C',
        'name': 'Triton1',
        'adc_bit_depth': 'Bits8',   # options are 'Bits8', 'Bits10', 'Bits12'
        'pixel_format': 'BayerRG8', # options are 'BayerRG8', 'BayerRG10', 'BayerRG12', 'Mono8', 'Mono10', 'Mono12'
        'fps': 196.,                # needs to be float, can be None if Max FPS available
        'exposure': 4_700.,         # needs to be float, can be None if Auto Exposure
        'bundle_size': 20,          # needs to be int
        'external_trigger': True,
        'verbose': True,
        'debug': True,
        'white_balance': {
            'BalanceWhiteAuto': 'Off',
            'BalanceWhiteEnable': False,
            'wb': [2.382813, 1, 2.314941]
        },
        'color_correction': {
            'ColorTransformationEnable': False,
            'ccm': [[1., 0, 0], [0, 1, 0], [0, 0, 1], [0, 0, 0]], # 
        }
    },
    {
        'skip': False,
        'maker': 'Triton',
        'serial_number': '253102178',
        'model': 'TRX051S-C',
        'name': 'Triton2',
        'adc_bit_depth': 'Bits8',   # options are 'Bits8', 'Bits10', 'Bits12'
        'pixel_format': 'BayerRG8', # options are 'BayerRG8', 'BayerRG10', 'BayerRG12'
        'fps': 196.,                # needs to be float, can be None if Max FPS available
        'exposure': 4_700.,         # needs to be float, can be None if Auto Exposure
        'bundle_size': 20,          # needs to be int
        'external_trigger': True,
        'verbose': True,
        'debug': True,
        'white_balance': {
            'BalanceWhiteAuto': 'Off',
            'BalanceWhiteEnable': False,
            'wb': [2.448975, 1, 2.314941]
        },
        'color_correction': {
            'ColorTransformationEnable': False,
            'ccm': [[1, 0, 0], [0, 1, 0], [0, 0, 1], [0, 0, 0]], # 
        }
    }
]



import sys
import os
sys.path.append(os.path.join(os.path.dirname(os.path.abspath(__file__)), '../'))
from src.numpy_io import NumpyWriter
from src.triton import TritonCam, create_triton
from src.basler import BaslerCam, create_basler

import threading
import time
import datetime

def create_camera(config, date):
    if config['maker'] == 'Triton':
        c = create_triton(config)
    elif config['maker'] == 'Basler':
        c = create_basler(config)
    else:
        print("Unknown camera, currently unsupported")
        return None, None

    w = NumpyWriter()
    w.filename_template = f"F:/triton_{date}/{config['name']}/%d"
    w.debug = False
    w.init()

    return c, w

# TOTALLY HACKY WAY
def writer_process(w: NumpyWriter, xs, meta, file_id):
    w.process(*xs,meta=meta, file_id=file_id)


def camera_loop(c: TritonCam | BaslerCam, w: NumpyWriter):
    file_id: int = 0
    while True:
        try:
            ret = c.process()
            if ret is not None:
                xs, meta = ret
                t=threading.Thread(target=writer_process, args=(w,xs,meta,file_id)) # TOTALLY HACKY WAY
                t.start()
                file_id+=1
                # w.process(*xs, meta=meta)
        except Exception as e:
            print(f"Error in camera {c.name}: {e}")
            c.finish()
            break

def main():
    cs: list[TritonCam | BaslerCam] = []
    ws: list[NumpyWriter] = []
    date = datetime.datetime.now().strftime("%Y_%m_%d_%H_%M_%S")

    # try:
    for config in DEVICE_CONFIGS:
        if config['skip']:
            continue

        c, w = create_camera(config, date)

        if c is None or w is None:
            print("Creating the device didn't work, check for errors")
            continue

        cs.append(c)
        ws.append(w)

    if len(cs) == 0:
        print("No cameras assigned, nothing to do here")
        sys.exit()

    threads = []
    for c, w in zip(cs, ws):
        t = threading.Thread(target=camera_loop, name=c.name, args=(c, w), daemon=True)
        t.start()
        threads.append(t)
    
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        for c in cs:
            c.finish()
        sys.exit()

if __name__ == '__main__':
    main()