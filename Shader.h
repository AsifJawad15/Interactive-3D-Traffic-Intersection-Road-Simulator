#pragma once

#include <glad/glad.h>
#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <string>
#include <unordered_map>

class Shader
{
public:
    Shader() = default;
    Shader(const std::string& vertexPath, const std::string& fragmentPath);
    ~Shader();

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    Shader(Shader&& other) noexcept;
    Shader& operator=(Shader&& other) noexcept;

    void use() const;
    GLuint id() const { return program_; }

    void setBool(const std::string& name, bool value) const;
    void setInt(const std::string& name, int value) const;
    void setFloat(const std::string& name, float value) const;
    void setVec2(const std::string& name, const glm::vec2& value) const;
    void setVec3(const std::string& name, const glm::vec3& value) const;
    void setVec4(const std::string& name, const glm::vec4& value) const;
    void setMat3(const std::string& name, const glm::mat3& value) const;
    void setMat4(const std::string& name, const glm::mat4& value) const;

    // Location of a uniform, looked up once and then cached. Asking the driver
    // by name on every draw was the most expensive part of the old renderer.
    GLint location(const std::string& name) const;

private:
    GLuint program_ = 0;
    mutable std::unordered_map<std::string, GLint> locations_;

    // Reads a shader source and expands  #include "file.glsl"  lines, resolved
    // relative to the including file, so shaders can share GLSL functions.
    static std::string readFile(const std::string& path, int depth = 0);
    static GLuint compile(GLenum type, const std::string& source, const std::string& label);
};
