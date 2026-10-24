#pragma once
#include <SDL_opengl.h>
#include <array>

namespace tinyengine
{
// Owns only deferred attachments. Depth, velocity and HDR remain owned by the scene target.
struct GlDeferredTargets
{
    std::array<GLuint, 4> textures{};
    GLuint geometry = 0, lighting = 0;
    bool active = false;
    template <class Gl> void destroy(Gl& gl)
    {
        if (geometry)
            gl.DeleteFramebuffers(1, &geometry);
        if (lighting)
            gl.DeleteFramebuffers(1, &lighting);
        glDeleteTextures(static_cast<GLsizei>(textures.size()), textures.data());
        textures.fill(0);
        geometry = lighting = 0;
        active = false;
    }
    template <class Gl>
    bool create(Gl& gl, int width, int height, GLuint depth, GLuint velocity, GLuint hdr, GLuint ambient)
    {
        destroy(gl);
        GLint attachments = 0, buffers = 0, units = 0;
        glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &attachments);
        glGetIntegerv(GL_MAX_DRAW_BUFFERS, &buffers);
        glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &units);
        if (attachments < 5 || buffers < 5 || units < 16)
            return false;
        gl.GenFramebuffers(1, &geometry);
        gl.BindFramebuffer(GL_FRAMEBUFFER, geometry);
        glGenTextures(4, textures.data());
        for (std::size_t i = 0; i < textures.size(); ++i)
        {
            glBindTexture(GL_TEXTURE_2D, textures[i]);
            glTexImage2D(GL_TEXTURE_2D, 0, i == 3 ? GL_RGBA8 : GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT,
                         nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(i), GL_TEXTURE_2D,
                                    textures[i], 0);
        }
        gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + 4, GL_TEXTURE_2D, velocity, 0);
        gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth, 0);
        const GLenum geometryOutputs[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT0 + 1, GL_COLOR_ATTACHMENT0 + 2,
                                          GL_COLOR_ATTACHMENT0 + 3, GL_COLOR_ATTACHMENT0 + 4};
        gl.DrawBuffers(5, geometryOutputs);
        bool complete = gl.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        gl.GenFramebuffers(1, &lighting);
        gl.BindFramebuffer(GL_FRAMEBUFFER, lighting);
        gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, hdr, 0);
        gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + 2, GL_TEXTURE_2D, ambient, 0);
        const GLenum lightOutputs[] = {GL_COLOR_ATTACHMENT0, GL_NONE, GL_COLOR_ATTACHMENT0 + 2};
        gl.DrawBuffers(3, lightOutputs);
        complete = complete && gl.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
        if (!complete)
            destroy(gl);
        return complete;
    }
};
} // namespace tinyengine
