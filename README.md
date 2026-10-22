# scanner-capture
Python Library for Controlling Hardware for 3D Scanning 

The CAD design and firmware code for the analog projector were moved to [this repo](https://github.com/geometryprocessing/analog-projector).

## Setup

```
pip install -r requirements.txt
cp configs/config.example.json configs/config.json
```

Camera SDKs are optional and not in `requirements.txt`:
- `arena_api` (Lucid Vision cameras, used by `src/atlas.py` and `src/triton.py`): install the wheel downloaded from the Lucid Vision website.
- `pypylon` (Basler cameras, used by `src/basler.py`): `pip install pypylon`.

Edit `configs/config.json` (git-ignored) for the machine. Relative paths in it are relative to the repo root.

| Key | Meaning |
| --- | --- |
| `data_path` | where captures are saved |
| `dark_path` | folder with the dark frames (`dark_frame_<exposure>_sec.npy` for `ldr`, `.exr` for `hdr`) |
| `patterns_root` | folder that contains `patterns/`; script `load` paths are relative to it |
| `scripts_dir` | folder with the `*.script` files (`configs/scripts`) |
| `camera_serial`, `packet_size` | Lucid camera serial number and GigE packet size |
| `stage` | `{"type": "linear" \| "rotating" \| null, "port": "COM3", "speed_divider": 24}` |
| `settle_frames` | frames to render after a pattern change before `ldr`/`hdr` |
| `subtract_blank` | passed to `Corrector` |
| `preload_patterns` | glob (relative to `patterns_root`) of patterns cached at startup |
| `hdr_exposures`, `ldr_exposure` | initial exposures in seconds |

Pattern images (`patterns/`) and dark frames (`dark_frames/`) are not in the repo; copy them to `patterns_root` and `dark_path`.

## Scanning

```
python scripts/static_scan.py [--config PATH] [--generate_scripts]
```

`--generate_scripts` regenerates the generated scripts in `scripts_dir` from the pattern folders before starting.
Other overrides: `--serial_number`, `--data_path`, `--dark_path`, `--patterns_root`, `--scripts_dir`, `--stage linear|rotating|none`, `--port`, `--settle_frames`.

Commands are read from the console (an empty line repeats the last command) and from scripts:

| Command | Effect |
| --- | --- |
| `load FILE` | project an image (path relative to `patterns_root`) |
| `color V` / `color R G B` [`c`] | solid color (`c`: center square only) |
| `checker [right\|left\|center [s]] [d] [r\|g\|b]`, `dots`, `gray I [v] [i]`, `stripes N [v]` | generated patterns |
| `patterns FOLDER` | queue `load` + `hdr` for every image in `patterns/FOLDER` |
| `ldr [EXPOSURE [NAME]]` | capture one dark-corrected frame (`.tiff`) |
| `hdr [NAME]` | capture an HDR image (`.exr`) with the current exposures |
| `exposures E1 E2 ...` | set HDR exposures (at least two supported values) |
| `settle N` | wait N rendered frames after a pattern change before `ldr`/`hdr` |
| `prefix DIR`, `suffix DIR` | output folder under `data_path` |
| `ldr_count N`, `hdr_count N`, `skip` | set or decrement the image counters |
| `move DIST`, `home` | move the stage (mm or degrees), home the linear stage |
| `delay SEC` | sleep |
| `process DIR [BLANK]` | post-process saved LDR images in `data_path/DIR` |
| `script NAME [force] [ignore_prefix]` | queue `scripts_dir/NAME.script` |
| `plot`, `save FILE`, `dump FILE`, `status`, `exit` | show/save the current pattern, save the command history, print state, quit |

`script NAME` expands `subscript` lines and checks that every loaded pattern exists and that generated pattern scripts still match their pattern folder. If a check fails, nothing is queued; regenerate with `--generate_scripts` or run anyway with `script NAME force`.
