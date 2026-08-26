#include "Shader.h"

#include <glm/gtc/type_ptr.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

Shader::Shader(const std::string& vertexPath, const std::string& fragmentPath)
{
    const std::string vertexSource = readFile(vertexPath);
    const std::string fragmentSource = readFile(fragmentPath);

    const GLuint vertex = compile(GL_VERTEX_SHADER, vertexSource, vertexPath);
    const GLuint fragment = compile(GL_FRAGMENT_SHADER, fragmentSource, fragmentPath);

    program_ = glCreateProgram();
    glAttachShader(program_, vertex);
    glAttachShader(program_, fragment);
    glLinkProgram(program_);

    GLint success = GL_FALSE;
    glGetProgramiv(program_, GL_LINK_STATUS, &success);
    if (success != GL_TRUE)
    {
        GLint length = 0;
        glGetProgramiv(program_, GL_INFO_LOG_LENGTH, &length);
        std::vector<char> log(static_cast<size_t>(length) + 1);
        glGetProgramInfoLog(program_, length, nullptr, log.data());
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        glDeleteProgram(program_);
        program_ = 0;
        throw std::runtime_error("Shader link failed:\n" + std::string(log.data()));
    }

    glDetachShader(program_, vertex);
    glDetachShader(program_, fragment);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
}

Shader::~Shader()
{
    if (program_ != 0)
        glDeleteProgram(program_);
}

Shader::Shader(Shader&& other) noexcept : program_(std::exchange(other.program_, 0))
{
}

Shader& Shader::operator=(Shader&& other) noexcept
{
    if (this != &other)
    {
        if (program_ != 0)
            glDeleteProgram(program_);
        program_ = std::exchange(other.program_, 0);
    }
    return *this;
}

void Shader::use() const
{
    glUseProgram(program_);
}

void Shader::setBool(const std::string& name, bool value) const
{
    glUniform1i(glGetUniformLocation(program_, name.c_str()), value ? 1 : 0);
}

void Shader::setInt(const std::string& name, int value) const
{
    glUniform1i(glGetUniformLocation(program_, name.c_str()), value);
}

void Shader::setFloat(const std::string& name, float value) const
{
    glUniform1f(glGetUniformLocation(program_, name.c_str()), value);
}

void Shader::setVec2(const std::string& name, const glm::vec2& value) const
{
    glUniform2fv(glGetUniformLocation(program_, name.c_str()), 1, glm::value_ptr(value));
}

void Shader::setVec3(const std::string& name, const glm::vec3& value) const
{
    glUniform3fv(glGetUniformLocation(program_, name.c_str()), 1, glm::value_ptr(value));
}

void Shader::setVec4(const std::string& name, const glm::vec4& value) const
{
    glUniform4fv(glGetUniformLocation(program_, name.c_str()), 1, glm::value_ptr(value));
}

void Shader::setMat3(const std::string& name, const glm::mat3& value) const
{
    glUniformMatrix3fv(glGetUniformLocation(program_, name.c_str()), 1, GL_FALSE, glm::value_ptr(value));
}

void Shader::setMat4(const std::string& name, const glm::mat4& value) const
{
    glUniformMatrix4fv(glGetUniformLocation(program_, name.c_str()), 1, GL_FALSE, glm::value_ptr(value));
}

std::string Shader::readFile(const std::string& path)
{
    const std::filesystem::path requested(path);
    const std::filesystem::path candidates[] = {
        requested,
        std::filesystem::current_path() / requested,
        std::filesystem::current_path() / "OpenGLMiniProject" / requested
    };

    for (const auto& candidate : candidates)
    {
        std::ifstream file(candidate, std::ios::binary);
        if (!file)
            continue;

        std::ostringstream stream;
        stream << file.rdbuf();
        return stream.str();
    }

    throw std::runtime_error("Could not open shader file: " + path);
}

GLuint Shader::compile(GLenum type, const std::string& source, const std::string& label)
{
    const GLuint shader = glCreateShader(type);
    const char* sourcePointer = source.c_str();
    glShaderSource(shader, 1, &sourcePointer, nullptr);
    glCompileShader(shader);

    GLint success = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (success != GL_TRUE)
    {
        GLint length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        std::vector<char> log(static_cast<size_t>(length) + 1);
        glGetShaderInfoLog(shader, length, nullptr, log.data());
        glDeleteShader(shader);
        throw std::runtime_error("Shader compilation failed (" + label + "):\n" + std::string(log.data()));
    }

    return shader;
}
