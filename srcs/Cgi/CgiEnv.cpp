#include "CgiEnv.hpp"
#include <cstring>
#include <cstdlib>
#include <sstream>

CgiEnv::CgiEnv(const Request &request, const std::string &script_path) : _envp(NULL)
{
    _buildMap(request, script_path);
    _convertToCStyle();
}

CgiEnv::~CgiEnv()
{
    _clear();
}

char **CgiEnv::getEnvp() const
{
    return _envp;
}

void CgiEnv::_buildMap(const Request &request, const std::string &script_path)
{
    _envMap["REQUEST_METHOD"] = request.getMethod();
    _envMap["QUERY_STRING"] = request.getQuery();
    _envMap["SCRIPT_FILENAME"] = script_path;

    if (request.getMethod() == "POST")
    {
        std::string len_str;
        if (request.getHeader("Content-Length", len_str))
        {
            _envMap["CONTENT_LENGTH"] = len_str;
        }
        else
        {
            _envMap["CONTENT_LENGTH"] = "0";
        }

        std::string content_type;
        if (request.getHeader("Content-Type", content_type))
        {
            _envMap["CONTENT_TYPE"] = content_type;
        }
        else
        {
            _envMap["CONTENT_TYPE"] = "";
        }
    }
}

void CgiEnv::_convertToCStyle()
{
    _envp = new char *[_envMap.size() + 1];
    size_t i = 0;
    for (std::map<std::string, std::string>::const_iterator it = _envMap.begin();
         it != _envMap.end(); ++it)
    {
        std::string env_entry = it->first + "=" + it->second;
        _envp[i] = strdup(env_entry.c_str());
        i++;
    }
    _envp[i] = NULL;
}

void CgiEnv::_clear()
{
    if (_envp != NULL)
    {
        for (size_t i = 0; _envp[i] != NULL; ++i)
        {
            free(_envp[i]);
        }
        delete[] _envp;
        _envp = NULL;
    }
}