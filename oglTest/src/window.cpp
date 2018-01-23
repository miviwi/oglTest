#include "window.h"

#include <cassert>
#include <algorithm>
#include <string>

#include <windowsx.h>

#include <GL/glew.h>
#include <GL/wglew.h>

#pragma comment(lib, "opengl32.lib")

namespace win32 {

static void APIENTRY ogl_debug_callback(GLenum source, GLenum type, GLuint id,
                                        GLenum severity, GLsizei length, GLchar *msg, const void *user);

Window::Window(int width, int height) :
  m_width(width), m_height(height)
{
  WNDCLASSEX wndClass;
  auto ptr = (unsigned char *)&wndClass;

  HINSTANCE hInstance = GetModuleHandle(nullptr);

  std::fill(ptr, ptr+sizeof(WNDCLASSEX), 0);

  wndClass.cbSize        = sizeof(WNDCLASSEX);
  wndClass.style         = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
  wndClass.lpfnWndProc   = &Window::WindowProc;
  wndClass.cbWndExtra    = sizeof(this);
  wndClass.hInstance     = hInstance;
  wndClass.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
  wndClass.lpszClassName = wnd_class_name();

  ATOM atom = RegisterClassEx(&wndClass);
  assert(atom && "failed to register window class!");

  RECT rc = { 0, 0, width, height };
  AdjustWindowRect(&rc, WS_CAPTION|WS_SYSMENU, false);

  m_hwnd = CreateWindow(wnd_class_name(), wnd_name(), WS_CAPTION|WS_SYSMENU,
                      CW_USEDEFAULT, CW_USEDEFAULT, rc.right-rc.left, rc.bottom-rc.top,
                      nullptr, nullptr, hInstance, 0);
  assert(m_hwnd && "failed to create window!");

  ShowWindow(m_hwnd, SW_SHOW);
  SetWindowLongPtr(m_hwnd, GWLP_USERDATA, (LONG_PTR)this);
}

bool Window::processMessages()
{
  MSG msg;
  while(PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE) != 0) {
    if(msg.message == WM_QUIT) return false;

    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }

  return true;
}

void Window::swapBuffers()
{
  wglSwapLayerBuffers(GetDC(m_hwnd), WGL_SWAP_MAIN_PLANE);
}

Input::Ptr Window::getInput()
{
  return m_input_man.getInput();
}

void Window::setMouseSpeed(float speed)
{
  m_input_man.setMouseSpeed(speed);
}

void Window::captureMouse()
{
  SetCapture(m_hwnd);
  resetMouse();

  while(ShowCursor(FALSE) >= 0);
}

void Window::releaseMouse()
{
  ReleaseCapture();

  while(ShowCursor(TRUE) < 0);
}

void Window::resetMouse()
{
  POINT pt = { 0, 0 };

  ClientToScreen(m_hwnd, &pt);
  SetCursorPos(pt.x + (m_width/2), pt.y + (m_height/2));
}

void Window::quit()
{
  PostMessage(m_hwnd, WM_CLOSE, 0, 0);
}

HGLRC Window::ogl_create_context(HWND hWnd)
{
  PIXELFORMATDESCRIPTOR pfd = {
    sizeof(PIXELFORMATDESCRIPTOR),
    1,
    PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER|PFD_DEPTH_DONTCARE,
    PFD_TYPE_RGBA,
    16,
    0, 0, 0, 0, 0, 0, 0, 0,
    0,
    0, 0, 0, 0,
    0, 0,
    0,
    PFD_MAIN_PLANE,
    0, 0, 0, 0,
  };

  HDC hdc = GetDC(hWnd);
  int pixel_format = ChoosePixelFormat(hdc, &pfd);

  assert(pixel_format && "cannot choose pixel format!");
  
  SetPixelFormat(hdc, pixel_format, &pfd);

  HGLRC temp_context = wglCreateContext(hdc);
  assert(temp_context && "failed to create temporary ogl context");

  wglMakeCurrent(hdc, temp_context);
  
  GLenum err = glewInit();
  if(err != GLEW_OK) {
    MessageBoxA(nullptr, "Failed to initialize GLEW!", "Fatal Error", MB_OK);
    ExitProcess(-1);
  }

  auto version_error = []()
  {
    MessageBoxA(nullptr, "OpenGL version >= 3.3 required!", "Fatal Error", MB_OK);
    ExitProcess(-1);
  };

  int flags = WGL_CONTEXT_DEBUG_BIT_ARB;

  int attribs[] = {
    WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
    WGL_CONTEXT_MINOR_VERSION_ARB, 3,
    WGL_CONTEXT_FLAGS_ARB, flags,
    0
  };

  if(!wglewIsSupported("WGL_ARB_create_context")) version_error();

  HGLRC context = wglCreateContextAttribsARB(hdc, nullptr, attribs);
  if(!context) version_error();

  wglMakeCurrent(nullptr, nullptr);
  wglDeleteContext(temp_context);

  wglMakeCurrent(hdc, context);

  wglSwapIntervalEXT(0);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

#if !defined(NDEBUG)
  if(!glewIsSupported("GL_KHR_debug")) {
    MessageBoxA(nullptr, "GL_KHR_debug not supported!", "Fatal Error", MB_OK);
    ExitProcess(-3);
  }

  glEnable(GL_DEBUG_OUTPUT);
  glDebugMessageCallback((GLDEBUGPROC)ogl_debug_callback, nullptr);
#endif

  return context;
}

LRESULT Window::WindowProc(HWND hWnd, UINT uMsg, WPARAM wparam, LPARAM lparam)
{
  auto self = (Window *)GetWindowLongPtr(hWnd, GWLP_USERDATA);

  switch(uMsg) {
  case WM_CREATE:
    ogl_create_context(hWnd);
    return 0;

  case WM_PAINT: {
    PAINTSTRUCT ps;
    BeginPaint(hWnd, &ps);
    EndPaint(hWnd, &ps);
    return 0;
  }

  case WM_INPUT:
    self->m_input_man.process((void *)lparam);
    self->resetMouse();
    return 0;

  case WM_SETFOCUS:
  case WM_ACTIVATE:
    if(self) self->captureMouse();
    return 0;

  case WM_KILLFOCUS:
    self->releaseMouse();
    return 0;

  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;

  default: return DefWindowProc(hWnd, uMsg, wparam, lparam);
  }

  return 0;
}

static char dbg_buf[1024];

static void APIENTRY ogl_debug_callback(GLenum source, GLenum type, GLuint id,
                               GLenum severity, GLsizei length, GLchar *msg, const void *user)
{
  const char *source_str = "";
  switch(source) {
  case GL_DEBUG_SOURCE_API:             source_str = "GL_API"; break;
  case GL_DEBUG_SOURCE_WINDOW_SYSTEM:   source_str = "GL_WINDOW_SYSTEM"; break;
  case GL_DEBUG_SOURCE_SHADER_COMPILER: source_str = "GL_SHADER_COMPILER"; break;
  case GL_DEBUG_SOURCE_APPLICATION:     source_str = "GL_APPLICATION"; break;
  case GL_DEBUG_SOURCE_THIRD_PARTY:     source_str = "GL_THIRD_PARTY"; break;
  case GL_DEBUG_SOURCE_OTHER:           source_str = "GL_OTHER"; break;
  }

  const char *type_str = "";
  switch(type) {
  case GL_DEBUG_TYPE_ERROR:               type_str = "error!"; break;
  case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: type_str = "deprecated"; break;
  case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:  type_str = "undefined!"; break;
  case GL_DEBUG_TYPE_PORTABILITY:         type_str = "portability"; break;
  case GL_DEBUG_TYPE_PERFORMANCE:         type_str = "performance"; break;
  case GL_DEBUG_TYPE_OTHER:               type_str = "other"; break;
  }

  const char *severity_str = "";
  switch(severity) {
  case GL_DEBUG_SEVERITY_HIGH:         severity_str = "HIGH"; break;
  case GL_DEBUG_SEVERITY_MEDIUM:       severity_str = "MEDIUM"; break;
  case GL_DEBUG_SEVERITY_LOW:          severity_str = "LOW"; break;
  case GL_DEBUG_SEVERITY_NOTIFICATION: severity_str = "NOTIFICATION"; break;
  }

  sprintf_s(dbg_buf, "%s (%s, %s): %s\n", source_str, severity_str, type_str, msg);

  OutputDebugStringA(dbg_buf);
}

}
