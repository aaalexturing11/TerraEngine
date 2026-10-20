#include "GlExt.h"

#include <iostream>

GlApi glx{};

template <typename T>
static T loadProc(const char* name) {
    void* p = (void*)glfwGetProcAddress(name);
    if (!p) std::cerr << "OpenGL: falta " << name << "\n";
    return reinterpret_cast<T>(p);
}

bool initGlExtensions() {
    glx.CreateShader = loadProc<PFNGLCREATESHADERPROC>("glCreateShader");
    glx.ShaderSource = loadProc<PFNGLSHADERSOURCEPROC>("glShaderSource");
    glx.CompileShader = loadProc<PFNGLCOMPILESHADERPROC>("glCompileShader");
    glx.GetShaderiv = loadProc<PFNGLGETSHADERIVPROC>("glGetShaderiv");
    glx.GetShaderInfoLog = loadProc<PFNGLGETSHADERINFOLOGPROC>("glGetShaderInfoLog");
    glx.DeleteShader = loadProc<PFNGLDELETESHADERPROC>("glDeleteShader");
    glx.CreateProgram = loadProc<PFNGLCREATEPROGRAMPROC>("glCreateProgram");
    glx.AttachShader = loadProc<PFNGLATTACHSHADERPROC>("glAttachShader");
    glx.LinkProgram = loadProc<PFNGLLINKPROGRAMPROC>("glLinkProgram");
    glx.GetProgramiv = loadProc<PFNGLGETPROGRAMIVPROC>("glGetProgramiv");
    glx.GetProgramInfoLog = loadProc<PFNGLGETPROGRAMINFOLOGPROC>("glGetProgramInfoLog");
    glx.UseProgram = loadProc<PFNGLUSEPROGRAMPROC>("glUseProgram");
    glx.DeleteProgram = loadProc<PFNGLDELETEPROGRAMPROC>("glDeleteProgram");
    glx.GetUniformLocation = loadProc<PFNGLGETUNIFORMLOCATIONPROC>("glGetUniformLocation");
    glx.Uniform1i = loadProc<PFNGLUNIFORM1IPROC>("glUniform1i");
    glx.Uniform1f = loadProc<PFNGLUNIFORM1FPROC>("glUniform1f");
    glx.Uniform2f = loadProc<PFNGLUNIFORM2FPROC>("glUniform2f");
    glx.Uniform3f = loadProc<PFNGLUNIFORM3FPROC>("glUniform3f");
    glx.Uniform4fv = loadProc<PFNGLUNIFORM4FVPROC>("glUniform4fv");
    glx.UniformMatrix4fv = loadProc<PFNGLUNIFORMMATRIX4FVPROC>("glUniformMatrix4fv");
    glx.GenFramebuffers = loadProc<PFNGLGENFRAMEBUFFERSPROC>("glGenFramebuffers");
    glx.BindFramebuffer = loadProc<PFNGLBINDFRAMEBUFFERPROC>("glBindFramebuffer");
    glx.FramebufferTexture2D = loadProc<PFNGLFRAMEBUFFERTEXTURE2DPROC>("glFramebufferTexture2D");
    glx.CheckFramebufferStatus = loadProc<PFNGLCHECKFRAMEBUFFERSTATUSPROC>("glCheckFramebufferStatus");
    glx.DeleteFramebuffers = loadProc<PFNGLDELETEFRAMEBUFFERSPROC>("glDeleteFramebuffers");
    glx.BlitFramebuffer = loadProc<PFNGLBLITFRAMEBUFFERPROC>("glBlitFramebuffer");
    glx.ActiveTexture = loadProc<PFNGLACTIVETEXTUREPROC>("glActiveTexture");
    glx.GenerateMipmap = loadProc<PFNGLGENERATEMIPMAPPROC>("glGenerateMipmap");
    glx.MultiTexCoord3f = loadProc<PFNGLMULTITEXCOORD3FPROC>("glMultiTexCoord3f");
    glx.MultiTexCoord4f = loadProc<PFNGLMULTITEXCOORD4FPROC>("glMultiTexCoord4f");
    glx.ClientActiveTexture = loadProc<PFNGLCLIENTACTIVETEXTUREPROC>("glClientActiveTexture");
    glx.GenBuffers = loadProc<PFNGLGENBUFFERSPROC>("glGenBuffers");
    glx.DeleteBuffers = loadProc<PFNGLDELETEBUFFERSPROC>("glDeleteBuffers");
    glx.BindBuffer = loadProc<PFNGLBINDBUFFERPROC>("glBindBuffer");
    glx.BufferData = loadProc<PFNGLBUFFERDATAPROC>("glBufferData");
    glx.GenVertexArrays = loadProc<PFNGLGENVERTEXARRAYSPROC>("glGenVertexArrays");
    glx.DeleteVertexArrays = loadProc<PFNGLDELETEVERTEXARRAYSPROC>("glDeleteVertexArrays");
    glx.BindVertexArray = loadProc<PFNGLBINDVERTEXARRAYPROC>("glBindVertexArray");
    return glx.CreateShader && glx.GenFramebuffers && glx.UseProgram && glx.BlitFramebuffer && glx.MultiTexCoord4f && glx.GenBuffers && glx.GenVertexArrays && glx.Uniform4fv;
}

static unsigned compileStage(unsigned type, const char* src) {
    unsigned s = glx.CreateShader(type);
    glx.ShaderSource(s, 1, &src, nullptr);
    glx.CompileShader(s);
    int ok = 0;
    glx.GetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glx.GetShaderInfoLog(s, 1024, nullptr, log);
        std::cerr << "Shader:\n" << log << "\n";
        glx.DeleteShader(s);
        return 0;
    }
    return s;
}

unsigned makeGlProgram(const char* vs, const char* fs) {
    unsigned v = compileStage(GL_VERTEX_SHADER, vs);
    unsigned f = compileStage(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) return 0;
    unsigned p = glx.CreateProgram();
    glx.AttachShader(p, v);
    glx.AttachShader(p, f);
    glx.LinkProgram(p);
    glx.DeleteShader(v);
    glx.DeleteShader(f);
    int ok = 0;
    glx.GetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glx.GetProgramInfoLog(p, 1024, nullptr, log);
        std::cerr << "Link:\n" << log << "\n";
        glx.DeleteProgram(p);
        return 0;
    }
    return p;
}
