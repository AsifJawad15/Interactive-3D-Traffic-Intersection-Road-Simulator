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

Shader::Shader(Shader&& other) noexcept
    : program_(std::exchange(other.program_, 0)),
      locations_(std::move(other.locations_))
{
}

Shader& Shader::operator=(Shader&& other) noexcept
{
    if (this != &other)
    {
        if (program_ != 0)
            glDeleteProgram(program_);
        program_ = std::exchange(other.program_, 0);
        locations_ = std::move(other.locations_);
    }
    return *this;
}

void Shader::use() const
{
    glUseProgram(program_);
}

GLint Shader::location(const std::string& name) const
{
    const auto found = locations_.find(name);
    if (found != locations_.end())
        return found->second;

    const GLint value = glGetUniformLocation(program_, name.c_str());
    locations_.emplace(name, value);
    return value;
}

void Shader::setBool(const std::string& name, bool value) const
{
    glUniform1i(location(name), value ? 1 : 0);
}

void Shader::setInt(const std::string& name, int value) const
{
    glUniform1i(location(name), value);
}

void Shader::setFloat(const std::string& name, float value) const
{
    glUniform1f(location(name), value);
}

void Shader::setVec2(const std::string& name, const glm::vec2& value) const
{
    glUniform2fv(location(name), 1, glm::value_ptr(value));
}

void Shader::setVec3(const std::string& name, const glm::vec3& value) const
{
    glUniform3fv(location(name), 1, glm::value_ptr(value));
}

void Shader::setVec4(const std::string& name, const glm::vec4& value) const
{
    glUniform4fv(location(name), 1, glm::value_ptr(value));
}

void Shader::setMat3(const std::string& name, const glm::mat3& value) const
{
    glUniformMatrix3fv(location(name), 1, GL_FALSE, glm::value_ptr(value));
}

void Shader::setMat4(const std::string& name, const glm::mat4& value) const
{
    glUniformMatrix4fv(location(name), 1, GL_FALSE, glm::value_ptr(value));
}

std::string Shader::readFile(const std::string& path, int depth)
{
    if (depth > 8)
        throw std::runtime_error("Shader #include nested too deeply: " + path);

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

        // Lines of the form   #include "name.glsl"   are replaced by the named
        // file, resolved relative to the file that includes it. GLSL has no
        // include mechanism of its own, so shared functions live in .glsl files.
        std::ostringstream expanded;
        std::string line;
        while (std::getline(file, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();

            const std::size_t firstCharacter = line.find_first_not_of(" \t");
            const bool isInclude = firstCharacter != std::string::npos &&
                                   line.compare(firstCharacter, 8, "#include") == 0;
            const std::size_t open = line.find('"');
            const std::size_t close = line.rfind('"');

            if (isInclude && open != std::string::npos && close > open)
            {
                const std::string included = line.substr(open + 1, close - open - 1);
                const std::filesystem::path includePath = candidate.parent_path() / included;
                expanded << readFile(includePath.string(), depth + 1) << '\n';
            }
            else
            {
                expanded << line << '\n';
            }
        }
        return expanded.str();
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
