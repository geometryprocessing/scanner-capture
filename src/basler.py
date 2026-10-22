import sys
import threading
import time
import datetime
import numpy as np
try:
    from pypylon import pylon
except ImportError:
    print("pypylon not installed, Basler cameras not supported")
    pylon = None
from .bundles import BundleCam
from .numpy_io import NumpyWriter

import os
os.environ["PYLON_CAMEMU"] = "0"

def camera_loop():
    while True:
        try:
            ret = c.process()
            if ret is not None:
                xs, meta = ret
                w.process(*xs, meta=meta)
        except Exception as e:
            print(f"Error in camera {c.name}: {e}")
            c.finish()
            break

class BaslerCam(BundleCam):
    name = 'BaslerCamera'
    global_time = None  # Global timestamp
    ext_trigger = False  # Use external trigger if True
    pixel_format = "BayerBG8"  # Supported pixel formats: BayerBG8, Mono8
    gain = 0.0  # Image gain, dB
    roi = None  # Full frame if None
    fps = None  # Max fps if None
    exp = None  # Auto if None, 0.99e+6/fps if 0, or us otherwise
    cam = None  # Actual camera handle
    white_balance = None  # Auto (on start) if None, default (5000 K) if "default", or 3-tuple otherwise

    def init(self, device):
        assert(self.pixel_format in ["BayerBG8", "Mono8"])

        self.cam = pylon.InstantCamera(pylon.TlFactory.GetInstance().CreateDevice(device))
        self.cam.Open()

        self.model_name = self.cam.GetDeviceInfo().GetModelName()
        self.cam.UserSetSelector.Value = "Default"
        self.cam.UserSetLoad.Execute()

        # self.cam.ReverseX.Value = True
        self.cam.PixelFormat.Value = "BayerBG8"
        self.cam.SensorReadoutMode.Value = 'Fast'
        self.cam.DeviceLinkThroughputLimitMode.Value = "Off"

        self.cam.LineSelector.Value = "Line4"
        self.cam.LineMode.Value = "Input"
        self.cam.LineSelector.Value = "Line3"
        self.cam.LineMode.Value = "Output"
        self.cam.LineSource.Value = "ExposureActive"
        self.cam.TriggerSource.Value = "Line4"
        self.cam.TriggerActivation.Value = "FallingEdge"

        if self.debug:
            print(self.name, "inited (%s)" % self.model_name)

        # time.sleep(2)  # Wait for the motor to settle before starting white balance calibration in init()
        self.start()   # Have plenty of time while object detector is being initialized
    
    def start(self):
        if self.roi is None:
            roi = (0, 0, self.cam.WidthMax.Value, self.cam.HeightMax.Value)
        self.optimal_exp = None

        self.cam.OffsetX.Value, self.cam.OffsetY.Value, self.cam.Width.Value, self.cam.Height.Value = roi
        self.cam.Gain.Value = self.gain
        self.gain = str(self.cam.Gain.Value) + " dB"

        if self.white_balance is None:
            self.cam.TriggerMode.Value = "Off"
            self.cam.AcquisitionFrameRateEnable.Value = True
            self.cam.AcquisitionFrameRate.Value = 200
            self.cam.ExposureAuto.Value = "Continuous"
            self.cam.BalanceWhiteAuto.Value = "Continuous"

            print("Estimating white balance for %s..." % self.model_name)
            self.cam.StartGrabbing(pylon.GrabStrategy_LatestImageOnly)
            time.sleep(3)
            self.cam.StopGrabbing()

            self.cam.BalanceWhiteAuto.Value = "Off"
            self.optimal_exp = str(self.cam.ExposureTime.Value / 1000.0) + " ms"
            self.white_balance = [None] * 3

        elif self.white_balance == "default":
            self.white_balance = [None] * 3

        elif type(self.white_balance) in [list, tuple]:
            assert(len(self.white_balance) == 3)

            self.cam.BalanceWhiteAuto.Value = "Off"

            for i, ch in enumerate(["Red", "Green", "Blue"]):
                self.cam.BalanceRatioSelector.Value = ch
                self.cam.BalanceRatio.Value = self.white_balance[i]
        else:
            print("Invalid white balance:", self.white_balance)
            self.white_balance = [None] * 3

        if self.ext_trigger:
            self.cam.AcquisitionFrameRateEnable.Value = False
            self.cam.TriggerMode.Value = "On"
        else:
            self.cam.TriggerMode.Value = "Off"
            self.cam.AcquisitionFrameRateEnable.Value = True
            self.fps = self.fps or 1.e+6 / self.cam.SensorReadoutTime.Value
            self.cam.AcquisitionFrameRate.Value = self.fps

        if self.exp is None:
            self.cam.ExposureAuto.Value = "Continuous"
            self.exposure = "Auto"
        else:
            self.cam.ExposureAuto.Value = "Off"
            self.cam.ExposureTime.Value = self.exp or 0.99e+6 / self.cam.AcquisitionFrameRate.Value
            self.exposure = str(self.cam.ExposureTime.Value / 1000.0) + " ms"

        for i, ch in enumerate(["Red", "Green", "Blue"]):
            self.cam.BalanceRatioSelector.Value = ch
            self.white_balance[i] = self.cam.BalanceRatio.Value

        self.fps = self.fps or self.cam.ResultingFrameRate.Value

        print("\nROI:", roi)
        print("Gain:", self.gain)
        print("White Balance:", self.white_balance)
        print("Exposure:", self.exposure, "\n")
        print("FPS:", self.fps, "(%.3f)" % self.cam.ResultingFrameRate.Value)

        self.bundle_reset()
        self.cam.OutputQueueSize = 100
        self.cam.StopGrabbing()  # clears the queue
        self.cam.StartGrabbing(pylon.GrabStrategy_OneByOne)
        # self.cam.StartGrabbing(pylon.GrabStrategy_LatestImages)
        # self.cam.StartGrabbing(pylon.GrabStrategy_LatestImageOnly)

        if self.debug:
            print(self.name, "started")

    # Implements BundleCam.get_buffer()
    def get_buffer(self):
        buffer, meta = None, {}

        if self.cam.IsGrabbing():
            grabResult = self.cam.RetrieveResult(1000, pylon.TimeoutHandling_Return)
            t = time.time()
            gt = self.global_time.value if self.global_time is not None else None

            if grabResult.IsValid():
                if grabResult.GrabSucceeded():
                    buffer = grabResult.Array
                    meta = { "python_timestamp" : t,
                                "global_timestamp" : gt,
                                "basler_timestamp" : grabResult.TimeStamp / 1.e+9,
                                "skipped_images" : grabResult.NumberOfSkippedImages,
                                "image_number" : grabResult.ImageNumber,
                                "external_trigger" : self.ext_trigger,
                                "pixel_format" : self.pixel_format,
                                "gain" : self.gain,
                                "white_balance" : self.white_balance,
                                "optimal_exposure" : self.optimal_exp,
                                "exposure" : self.exposure,
                                "fps" : self.fps,
                                "roi" : self.roi,
                                "resolution" : buffer.shape[:2],
                                "grab_id" : grabResult.ID}

                    if self.debug and self.verbose:
                        print("New buffer:", meta)
                else:
                    if self.debug:
                        print("Grab failed!")
                        
                grabResult.Release()
            else:
                if self.debug:
                    print("Invalid grab result!")
        else:
            raise RuntimeError("Basler camera not grabbing!")

        return buffer, meta

    # Provided by BundleCam
    # def process(self, *xs, meta=None): 
    #     ...

    def stop(self):
        if self.cam:
            self.cam.StopGrabbing()

        self.bundle_reset()

        if self.debug:
            print(self.name, "stopped")

    def finish(self):
        self.stop()

        if self.cam:
            if self.cam.IsOpen():
                self.cam.Close()

        if self.debug:
            print(self.name, "finished")

def create_basler(config: dict) -> BaslerCam:
    ## TODO: do the same check for serial number for basler
    tlFactory = pylon.TlFactory.GetInstance()
    devices = tlFactory.EnumerateDevices()
    if len(devices) == 0:
        print("No Basler camera detected.")
        return None, None

    for idx, device in enumerate(devices, start=1):
        c = BaslerCam()

        c.name = str(config['name'])
        c.pixel_format = str(config['pixel_format'])
        c.fps = float(config['fps'])
        c.exp = float(config['exposure'])
        c.bundle = int(config['bundle_size'])
        c.ext_trigger = bool(config['external_trigger'])
        c.verbose = bool(config['verbose'])
        c.debug = bool(config['debug'])
        c.white_balance = config['white_balance']

        c.init(device)
        print("Debug:", c.debug, ", Verbose:", c.verbose)

        return c
