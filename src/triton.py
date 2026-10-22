import ctypes

import os
import sys
import threading

import datetime
import time

import numpy as np

from .bundles import BundleCam
from .numpy_io import NumpyWriter

from arena_api.system import system
from arena_api.callback import callback, callback_function
from arena_api._device import Device
from arena_api.buffer import _Buffer
# from arena_api.buffer import BufferFactory

def wb_parser(
        wb: list[float]=[1.,1.,1.]) -> list:
    """
    Function to parse through white balance list and write it
    onto a dictionary for Triton camera API.

    Parameters
    ----------
    wb : list of floats, optional
        shape should be 
        [ Red, Green, Blue ]
        default value is [1., 1., 1.]

    Returns
    -------
    list with data for Triton white balancing

    NOTE
    ----
    Do not change the order of the list
    """
    return [
            ('BalanceRatioSelector', 'Red'),
            ('BalanceRatio', float(wb[0])),
            ('BalanceRatioSelector', 'Green'),
            ('BalanceRatio', float(wb[1])),
            ('BalanceRatioSelector', 'Blue'),
            ('BalanceRatio', float(wb[2])),
    ]
    

def ccm_parser(
        ccm: list[list[float]]=[[1,0,0], [0,1,0], [0,0,1], [0, 0, 0]]) -> list:
    """
    Function to parse through color correction matrix and offset and 
    write it onto 

    ccm : list of list of floats, optional
        shape should be 
        [[    Gain00,       Gain01,      Gain02],
         [    Gain10,       Gain11,      Gain12],
         [    Gain20,       Gain21,      Gain22],
         [offset_red, offset_green, offset_blue]]
        default is identity matrix
    Returns
    -------
    list with data for Triton color correction

    NOTE
    ----
    Do not change the order of the list
    """

    return [
            ('ColorTransformationValueSelector', 'Gain00'),
            ('ColorTransformationValue', float(ccm[0][0])),
            ('ColorTransformationValueSelector', 'Gain01'),
            ('ColorTransformationValue', float(ccm[0][1])),
            ('ColorTransformationValueSelector', 'Gain02'),
            ('ColorTransformationValue', float(ccm[0][2])),
            ('ColorTransformationValueSelector', 'Gain10'),
            ('ColorTransformationValue', float(ccm[1][0])),
            ('ColorTransformationValueSelector', 'Gain11'),
            ('ColorTransformationValue', float(ccm[1][1])),
            ('ColorTransformationValueSelector', 'Gain12'),
            ('ColorTransformationValue', float(ccm[1][2])),
            ('ColorTransformationValueSelector', 'Gain20'),
            ('ColorTransformationValue', float(ccm[2][0])),
            ('ColorTransformationValueSelector', 'Gain21'),
            ('ColorTransformationValue', float(ccm[2][1])),
            ('ColorTransformationValueSelector', 'Gain22'),
            ('ColorTransformationValue', float(ccm[2][2])),
            ('ColorTransformationValueSelector', 'Offset0'),
            ('ColorTransformationValue', float(ccm[3][0])),
            ('ColorTransformationValueSelector', 'Offset1'),
            ('ColorTransformationValue', float(ccm[3][1])),
            ('ColorTransformationValueSelector', 'Offset2'),
            ('ColorTransformationValue', float(ccm[3][2])),
    ]



class TritonCam(BundleCam):
    ### below are the defaults
    name = 'TritonCamera'
    global_time = None  # Global timestamp
    ext_trigger = False  # Use external trigger if True
    pixel_format = "BayerRG8"  # Supported pixel formats: BayerRG8, BayerRG10, BayerRG12
    adc_bit_depth = 8
    gain = 0.0  # Image gain, dB
    roi = None  # Full frame if None
    fps = None  # Max fps if None
    exp = None  # Auto if None, 0.99e+6/fps if 0, or us otherwise
    device = None  # Actual camera handle
    white_balance = None  # Auto (on start) if None, default (5000 K) if "default", or 3-tuple otherwise
    color_correction = None

    def init(self, device: Device):
        assert(self.pixel_format in ["BayerRG8", "BayerRG10" "BayerRG12", "Mono8", "Mono10", "Mono12"])
        assert(self.adc_bit_depth in ['Bits8','Bits10','Bits12']), "ADC Bit Depth only 8, 10, or 12"

        self.device = device
        self.nm = self.device.nodemap

        self.nm['UserSetSelector'].value = 'Default'
        self.nm['UserSetLoad'].execute()

        # self.model_name = self.device.GetDeviceInfo().GetModelName()

        self.nm['Width'].value = self.nm['Width'].max if not self.roi else self.roi[0]
        self.nm['Height'].value = self.nm['Height'].max if not self.roi else self.roi[1]
        self.nm['OffsetX'].value = self.nm['OffsetX'].min if not self.roi else self.roi[2]
        self.nm['OffsetY'].value = self.nm['OffsetY'].min if not self.roi else self.roi[3]
        self.nm['PixelFormat'].value = self.pixel_format
        self.nm['ADCBitDepth'].value = self.adc_bit_depth

        self.roi = (self.nm['Width'].value, self.nm['Height'].value, self.nm['OffsetX'].value, self.nm['OffsetY'].value)
        self.pixel_format = self.nm['PixelFormat'].value

        print("\nFrame Format:\n\t", self.roi, self.pixel_format)

        if self.debug:
            print(self.name, "initialized")

        # time.sleep(2)  # Wait for the motor to settle before starting white balance calibration in init()
        self.start()   # Have plenty of time while object detector is being initialized
    
    def start(self):

        # should help with missing image data but doesn't (just increases delay)
        self.device.tl_stream_nodemap['StreamBufferHandlingMode'].value = 'NewestOnly'
        self.device.tl_stream_nodemap['StreamPacketResendEnable'].value = True
        self.device.tl_stream_nodemap['StreamMaxNumResendRequestsPerImage'].value = 100
        self.device.tl_stream_nodemap['StreamAutoNegotiatePacketSize'].value = False
        self.nm['GevSCPSPacketSize'].value = 9_000
        self.nm['TransportStreamProtocol'].value = 'RDMA'
        print("Packet Size:", self.nm['GevSCPSPacketSize'].value)

        self.nm['AcquisitionMode'].value = 'Continuous'
        if self.exp is None:
            self.nm['ExposureAuto'].value = 'Continuous'
            self.exposure = "Auto"
        else:
            self.nm['ExposureAuto'].value = 'Off'
            self.nm['ExposureTime'].value = self.exp or 0.99e+6 / self.nm['AcquisitionFrameRate'].value
            self.exposure = str(self.nm['ExposureTime'].value / 1000.0) + " ms"

        self.nm['AcquisitionFrameRateEnable'].value = True
        self.fps = self.fps or self.nm['AcquisitionFrameRate'].max # if self.fps is not given, pick max acquisition frame rate of camera
        self.nm['AcquisitionFrameRate'].value = self.fps
        if self.ext_trigger:
            self.nm['TriggerMode'].value = "On"
            self.nm['TriggerSelector'].value = "FrameStart" #"ExposureActive"
            # self.nm['AcquisitionFrameRateEnable'].value = True
            self.nm['TriggerSource'].value = 'Line2'
            self.nm['TriggerActivation'].value = 'FallingEdge'
            self.nm['TriggerOverlap'].value = 'PreviousFrame' # this doesn't work with ExposureActive
        else:
            self.nm['TriggerMode'].value = "Off"
            # self.nm['AcquisitionFrameRateEnable'].value = True
            self.nm['TriggerSource'].value = 'Software'

        for key, value in self.white_balance.items():
            if key == 'wb':
                wb = wb_parser(value)
                for (key, value) in wb:                    
                    self.nm[key].value = value
            else:
                self.nm[key].value = value

        ### TODO: Need to figure out here the color correction matrix.
        ### Since it's 12 terms, maybe just let the user write a 3x3 matrix with the offset and have
        ### the code here parse through it?
        self.nm['ColorTransformationEnable'].value = False

        for key, value in self.color_correction.items():
            if key == 'ccm':
                ccm = ccm_parser(value)
                for (key, value) in ccm:                    
                    self.nm[key].value = value
            else:
                self.nm[key].value = value

        if self.verbose:
            print("\nROI:", self.roi)
            print("Gain:", self.gain)
            print("White Balance:", self.white_balance)
            print("Color Correction:", self.color_correction)
            print("Exposure:", self.exposure, "\n")
            print("FPS:", self.fps, "(actual %.3f)" % self.nm['AcquisitionFrameRate'].value)


        self.bundle_reset()
        self.device.stop_stream()  # clears the queue
        # self.cam.OutputQueueSize = 100 # we had 100 buffers in output queue size for Basler
        self.device.start_stream(500) # output queue size of 500 buffers

    # Implements BundleCam.get_buffer()
    def get_buffer(self) -> tuple[np.ndarray, dict]:
        data, meta = None, {}

        if self.device.tl_stream_nodemap['StreamIsGrabbing']:
            buffer: _Buffer = self.device.get_buffer()
            
            ## HACKY WAY TO THROW AWAY THE FIRST BUFFER
            if buffer.frame_id == 1:
                return data, meta
            ##
            
            t = time.time()
            gt = self.global_time.value if self.global_time is not None else None

            
            if buffer.has_imagedata:
                if buffer.is_incomplete:
                    if self.debug:
                        print("Grab failed!")
                else:
                    
                    ### TODO: understand this code originally from Yurii
                    # ~ 20-30 ms (depends on pixel format)
                    # p = ctypes.cast(buffer.pdata, ctypes.POINTER(ctypes.c_uint8 if self.pixel_format in ["Mono8", "BayerRG8"] else ctypes.c_uint16))
                    # data = np.copy(np.ctypeslib.as_array(p, (buffer.height, buffer.width)))
                    data = np.copy(np.ctypeslib.as_array(buffer.pdata, (buffer.height, buffer.width)))

                    meta = { "python_timestamp" : t,
                                "global_timestamp" : gt,
                                "triton_timestamp" : buffer.timestamp_ns / 1.e+9,
                                "skipped_images" : self.device.tl_stream_nodemap['StreamLostFrameCount'].value,
                                "image_number" : buffer.frame_id,
                                "external_trigger" : self.ext_trigger,
                                "pixel_format" : self.pixel_format,
                                "gain" : self.gain,
                                "white_balance" : self.white_balance,
                                "exposure" : self.exposure,
                                "fps" : self.fps,
                                "roi" : self.roi,
                                "resolution" : (buffer.width, buffer.height),
                                }

                    if self.debug and self.verbose:
                        print(f"New buffer ({self.name}): {meta}")
            else:
                if self.debug:
                    print("Invalid grab result!")
        else:
            raise RuntimeError("Triton camera not grabbing!")
        
        self.device.requeue_buffer(buffer)

        return data, meta

    # Provided by BundleCam
    # def process(self, *xs, meta=None): 
    #     ...

    def stop(self):
        if self.device:
            self.device.stop_stream()

        self.bundle_reset()
        if self.debug:
            print(self.name, "stopped")

    def finish(self):
        self.stop()

        if self.device:
            if self.device.is_connected():
                system.destroy_device(self.device)
            self.device = None

        if self.debug:
            print(self.name, "finished")

def create_triton(config: dict) -> TritonCam:
    device_infos: list = system.device_infos
    if len(device_infos) == 0:
        print("No Triton camera detected.")
        return None
    
    sn = config['serial_number']
    device_found = False
    device_info_to_create = None
    for device_info in device_infos:
        if device_info['serial'] == sn:
            device_found = True
            device_info_to_create = device_info
            break

    if not device_found:
        print('Did not find serial number ', sn)
        return None

    c = TritonCam()
    c.name = str(config['name'])
    c.adc_bit_depth = str(config['adc_bit_depth'])
    c.pixel_format = str(config['pixel_format'])
    c.fps = float(config['fps'])
    c.exp = None if config['exposure'] is None else float(config['exposure'])
    c.bundle = int(config['bundle_size'])
    c.ext_trigger = bool(config['external_trigger'])
    c.verbose = bool(config['verbose'])
    c.debug = bool(config['debug'])
    c.white_balance = config['white_balance']
    c.color_correction = config['color_correction']

    device = system.create_device(device_infos=device_info_to_create)[0]

    c.init(device)
    print("Debug:", c.debug, ", Verbose:", c.verbose)

    return c