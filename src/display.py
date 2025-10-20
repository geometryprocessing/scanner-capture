import time
import queue
import threading
import cv2
import numpy as np
import matplotlib.pyplot as plt

from .utils import (gen_checker, gen_color,
      gen_dots, gen_gray, gen_stripes)

# GL
import glfw
from OpenGL.GL import *


# CHANGE HEIGHT AND WIDTH OF MONITOR YOU WANT TO USE
H, W = 1080, 1920


######################################### GL utils ##########################################
#############################################################################################
def opengl_error_check():
    error = glGetError()
    if error != GL_NO_ERROR:
        print("OPENGL_ERROR: ", gluErrorString(error))

vertexSize = 5 # we enter vec3 pos and vec2 texCoords into vertex shader

vertexShader = """
#version 410 core

in vec3 aPos;
in vec2 texCoords;

out vec2 vUV;

void main() {
  gl_Position = vec4(aPos, 1.0); 
  vUV = texCoords;
}
"""

fragmentShader = """
#version 410 core

in vec2 vUV;

out vec4 color;

uniform sampler2D tex;

void main() {
    color = texture(tex, vUV);
}
"""

def vertexAttribute(program, name: str, size: int, position: int):
    attr = glGetAttribLocation(program, name)
    glEnableVertexAttribArray(attr)
    glVertexAttribPointer(attr, size, GL_FLOAT, GL_FALSE, vertexSize * 4, ctypes.c_void_p(position * 4))

def addshader(program, shader_type, src):
    shader = glCreateShader(shader_type)
    glShaderSource(shader, src)
    glCompileShader(shader)
    if not (glGetShaderiv(shader, GL_COMPILE_STATUS)):
        raise RuntimeError(f"Cannot compile shader:\n\n {glGetShaderInfoLog(shader)}")
    glAttachShader(program, shader)

#############################################################################################
#############################################################################################

running = True

class Pattern:
    def __init__(self):
        self.vao = glGenVertexArrays(1)
        glBindVertexArray(self.vao)
        
        # Generate vertex buffer object
        quad = np.array([-1,  1, 0, 0, 0,
                          1,  1, 0, 1, 0,
                         -1, -1, 0, 0, 1,
                          1, -1, 0, 1, 1], np.float32)
        self.vbo = glGenBuffers(1)
        glBindBuffer(GL_ARRAY_BUFFER, self.vbo)
        glBufferData(GL_ARRAY_BUFFER, quad.nbytes, quad, GL_STATIC_DRAW)

        # create program, compile and link shaders
        self.program = glCreateProgram()
        addshader(self.program, GL_VERTEX_SHADER  , vertexShader  )
        addshader(self.program, GL_FRAGMENT_SHADER, fragmentShader)
        glBindFragDataLocation(self.program, 0, "color")
        glLinkProgram(self.program)
        if not (glGetProgramiv(self.program, GL_LINK_STATUS)):
            raise RuntimeError("Could not link the shader program!")
        glUseProgram(self.program)

        # enable vertex attributes
        vertexAttribute(self.program, "aPos", 3, 0)
        vertexAttribute(self.program, "texCoords", 2, 3)
        
        # create texture to store pattern
        self.texture = glGenTextures(1)
        self.tex = 0
        
        glActiveTexture(GL_TEXTURE0 + self.tex)
        glBindTexture(GL_TEXTURE_2D, self.texture)
        self.rgb = None
        self.update(np.zeros((H, W, 3), dtype=np.uint8))

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE)

        tex = glGetUniformLocation(self.program, "tex")
        glUniform1i(tex, self.tex)
        
    def update(self, rgb):
        self.rgb = rgb
        # add alpha channel
        new_texture = np.zeros((H, W, 4), dtype=np.uint8)
        new_texture[:, :, :3] = rgb[:, :, :3]

        # upload flipped vertically
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, new_texture)

    def render(self):
        glClearColor(0.5, 0.5, 0.5, 1.0)
        glClear(GL_COLOR_BUFFER_BIT)
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4)

class Projector:
    def __init__(self):
        glfw.init()

        # detect projector by matching resolution
        projector = None
        for monitor in glfw.get_monitors():
            mode = glfw.get_video_mode(monitor)
            if mode.size.width == W and mode.size.height == H:
                projector = monitor

        if projector is None:
            raise RuntimeError(f"Did not find the correct monitor with {W}x{H} resolution")

        # windowed full screen
        glfw.window_hint(glfw.AUTO_ICONIFY, False)
        glfw.window_hint(glfw.CONTEXT_VERSION_MAJOR, 4)
        glfw.window_hint(glfw.CONTEXT_VERSION_MINOR, 1)
        glfw.window_hint(glfw.OPENGL_FORWARD_COMPAT, GL_TRUE)
        glfw.window_hint(glfw.OPENGL_PROFILE, glfw.OPENGL_CORE_PROFILE)

        self.window = glfw.create_window(W, H, "Projector", projector, None)
        glfw.make_context_current(self.window)

        glfw.set_key_callback(self.window, self.on_keyboard)
        glfw.set_input_mode(self.window, glfw.CURSOR, glfw.CURSOR_HIDDEN)
        # glfw.set_input_mode(self.window, glfw.CURSOR, glfw.CURSOR_DISABLED)

        self.pattern = Pattern()

    def on_keyboard(self, window, key, scancode, action, mods):
        if action == glfw.PRESS:
            if key == glfw.KEY_ESCAPE:
                glfw.set_window_should_close(window, True)

    def should_close(self):
        return glfw.window_should_close(self.window)

    def get_pattern(self):
        return self.pattern.rgb

    def update(self, new_pattern=None):
        if new_pattern is not None:
            self.pattern.update(new_pattern)

        self.pattern.render()
        glfw.swap_buffers(self.window)
        glfw.poll_events()

def console_input(input_queue):
    last_input = None
    print('Enter Commands:')
    # global running
    while running:
        new_input = input()
        if new_input == "" and last_input is not None:
            new_input = last_input
        else:
            last_input = new_input
        input_queue.put(new_input)


def parallel_input():
    input_queue = queue.Queue()
    input_thread = threading.Thread(target=console_input, daemon=True, args=(input_queue,))
    input_thread.start()
    return input_queue, input_thread


if __name__ == "__main__":

    try:
        projector = Projector()
        input_queue, thr = parallel_input()
        input_queue.put("checker")
        i = 0

        while not projector.should_close():
            new_pattern = None
            if not input_queue.empty():
                cmd = input_queue.get()
                print("Got:", cmd)
                if cmd == 'red':
                    patt = np.zeros((H, W, 3), dtype=np.uint8)
                    patt[:, :, 0] = np.random.randint(255, size=(H, W))
                    new_pattern = patt
                if cmd == 'w':
                    new_pattern = gen_color((H, W), color=(255, 255, 255))
                if cmd == 'k':
                    new_pattern = gen_color((H, W), color=(0, 0, 0))
                if cmd == 'r':
                    new_pattern = gen_color((H, W), color=(255, 0, 0))
                if cmd == 'g':
                    new_pattern = gen_color((H, W), color=(0, 255, 0))
                if cmd == 'b':
                    new_pattern = gen_color((H, W), color=(0, 0, 255))
                if cmd == 'color':
                    new_pattern = gen_color((H, W), color=(128, 255, 128))
                if cmd == 'stripes':
                    new_pattern = gen_stripes((H, W), 1, axis=0)
                if cmd == 'gray':
                    R, C = gen_gray((H, W), color=[255, 0, 0])
                    new_pattern = R[i, :, :, :]
                if cmd == 'u':
                    i += 1
                    R, C = gen_gray((H, W), color=[255, 0, 0])
                    new_pattern = R[i, :, :, :]
                if cmd == 'd':
                    i -= 1
                    R, C = gen_gray((H, W), color=[255, 0, 0])
                    new_pattern = R[i, :, :, :]
                if cmd == 'dots':
                    new_pattern = gen_dots((H, W), (90 + 50, 60 + 50), (100, 100), (9 - 1, 18 - 1))
                if cmd == 'checker':
                    new_pattern = gen_checker((H, W), (90, 60), 100, (9, 18))
                if cmd == 'both':
                    new_pattern_1 = gen_checker((H, W), (90, 60), 100, (9, 18))
                    new_pattern_2 = gen_dots((H, W), (90 + 50, 60 + 50), (100, 100), (9 - 1, 18 - 1))
                    new_pattern = np.maximum(new_pattern_1, new_pattern_2)
                if cmd == 'checker r':
                    new_pattern = gen_checker((H, W), (90, 60), 100, (9, 18))
                    new_pattern *= np.array([1, 0, 0], dtype=np.uint8)
                if cmd == 'checker g':
                    new_pattern = gen_checker((H, W), (90, 60), 100, (9, 18))
                    new_pattern *= np.array([0, 1, 0], dtype=np.uint8)
                if cmd == 'checker b':
                    new_pattern = gen_checker((H, W), (90, 60), 100, (9, 18))
                    new_pattern *= np.array([0, 0, 1], dtype=np.uint8)
                if cmd == 'checker t':
                    # new_pattern = gen_checker((H, W), (60, 160), 100, (7, 16))
                    new_pattern = gen_checker((H, W), (40, 180), 80, (9, 20))
                    new_pattern *= np.array([0, 1, 0], dtype=np.uint8)
                    # new_pattern[H-260:, :, :] = 0
                    new_pattern[H-280:, :, :] = 0
                if cmd == 'g t':
                    new_pattern = gen_color((H, W), color=(0, 255, 0))
                    new_pattern[H-280:, :, :] = 0
                if cmd[:4] == 'load':
                    try:
                        new_pattern = cv2.imread(cmd[5:])[:, :, ::-1]
                    except:
                        print('No file... try again')
                if cmd[:4] == 'save':
                    cv2.imwrite(cmd[5:]+".png", projector.get_pattern()[:, :, ::-1])
                if cmd == 'exit':
                    break

            projector.update(new_pattern)
            plt.show(block=False)
            time.sleep(0.02)

    finally:
        glfw.terminate()
    print('Done')