#pragma once

#include <glad/glad.h>
#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <functional>
#include <string>
#include <string_view>
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

    void setBool(std::string_view name, bool value) const;
    void setInt(std::string_view name, int value) const;
    void setFloat(std::string_view name, float value) const;
    void setVec2(std::string_view name, const glm::vec2& value) const;
    void setVec3(std::string_view name, const glm::vec3& value) const;
    void setVec4(std::string_view name, const glm::vec4& value) const;
    void setMat3(std::string_view name, const glm::mat3& value) const;
    void setMat4(std::string_view name, const glm::mat4& value) const;

    // Location of a uniform, looked up once and then cached. Asking the driver
    // by name on every draw was the most expensive part of the old renderer.
    GLint location(std::string_view name) const;

private:
    GLuint program_ = 0;
    // Looked up by string_view, so passing a literal never builds a string.
    struct NameHash
    {
        using is_transparent = void;
        std::size_t operator()(std::string_view name) const noexcept
        {
            return std::hash<std::string_view> {}(name);
        }
    };
    mutable std::unordered_map<std::string, GLint, NameHash, std::equal_to<>> locations_;

    // Reads a shader source and expands  #include "file.glsl"  lines, resolved
    // relative to the including file, so shaders can share GLSL functions.
    static std::string readFile(const std::string& path, int depth = 0);
    static GLuint compile(GLenum type, const std::string& source, const std::string& label);
};
