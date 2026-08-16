#ifndef REQUEST_HANDLER_INTERNAL_HPP
#define REQUEST_HANDLER_INTERNAL_HPP
#include "RequestHandler.hpp"

// Changes ASCII letters in the text to lower case.
std::string requestHandlerToLowerAscii(const std::string &value);

// Returns mime type.
std::string getMimeType(const std::string &path);

// Handles index.
Response handleIndex(const EffectiveRoute &route, const std::string &requestPath, bool closeConnection);
enum FileOperationStatus
{
    FILE_OPERATION_OK = 0, FILE_OPERATION_ERROR = -1
};
struct FileOperation
{
    std::string fileName;
    std::string filePath;
    Response response;
    // Creates a new FileOperation object.
    explicit FileOperation(const Request &request) : fileName(), filePath(), response(request)
    {
    }
    // Returns file name.
    int getFileName(const EffectiveRoute &route);
    // Checks upload request.
    int validateUploadRequest(const Request &request, const EffectiveRoute &route);
    // Creates file.
    int createFile(const std::string &body, const std::string &baseDirectory, const Response::ErrorPageMap &errorPages);
    // Checks whether the target file exists.
    bool fileExists(const std::string &fullPath) const;
    // Builds a file name that does not overwrite an existing file.
    std::string generateUniqueFilename(const std::string &baseDirectory) const;
    // Creates delete response.
    void createDeleteResponse(int errorNumber, const Response::ErrorPageMap &errorPages);
};
#endif
