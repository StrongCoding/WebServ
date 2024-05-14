#include "HttpResponse.hpp"
#include <sys/socket.h>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <dirent.h>
#include "Logger.hpp"
#include "Config.hpp"

static bool isFolder(const std::string &path)
{
	struct stat s;
	if (stat(path.c_str(), &s) == 0)
	{
		if (s.st_mode & S_IFDIR)
			return true;
	}
	return false;
}

static bool isFile(const std::string &path)
{
	struct	stat s;
	if (stat(path.c_str(), &s) == 0)
	{
		if (s.st_mode & S_IFREG)
			return true;
	}
	return false;
}

HttpResponse::HttpResponse(HttpHeader &header, int fds) : header(header), fds(fds) {
	LOG_DEBUG("HttpResponse::HttpResponse");
	config = Config::getInstance();
	response = "HTTP/1.1 ";
	isFinished = false;
	error = header.getError();
	if (error.code() != 0)
	{
		response = generateErrorResponse(error.message());
		return;
	}

	std::string host = header.getHost();
	std::string path = header.getPath();
	LOG_DEBUG(config->getDirectiveValue(path, host, Config::Redir).c_str());
	if (config->getDirectiveValue(path, host, Config::Redir).size())
	{
		LOG_DEBUG("Redirect");
		response += "308 Permanent Redirect\r\n";
		response += "Location: " + config->getDirectiveValue(path, header.getHost(), Config::Redir);
		response += "\r\n\r\n";
		return;
	}
	else if (header.getMethod() == "GET" && config->isDirectiveAllowed(path, host, Config::AllowedMethods, "GET"))
	{
		LOG_DEBUG("GET request");
		if (isFile(config->getFilePath(path, host)))
		{
			LOG_DEBUG(config->getFilePath(path, host).c_str());
			LOG_DEBUG("returning file");
			std::string filePath = config->getFilePath(path, host);
			LOG_DEBUG(filePath);
			getFile.open(filePath.c_str());
			if (getFile.fail())
				error = HttpError(500, "Couldn't open File");
			LOG_DEBUG("File opened");
			response += "200 OK\r\n";
			response += "Connection: close\r\n";
			response += "transfer-encoding: chunked\r\n\r\n";
		}
		else if (isFolder(config->getFilePath(path, host)))
		{
			if (config->getDirectiveValue(path, header.getHost(), Config::Index).size() != 0)
			{
				LOG_DEBUG("returning index file");
				std::string filePath = config->getFilePath(path, host) + config->getDirectiveValue(path, header.getHost(), Config::Index);
				getFile.open(filePath.c_str());
				if (getFile.fail())
				{
					if (config->getDirectiveValue(path, header.getHost(), Config::Listing) == "on")
					{
						LOG_DEBUG("returning listing");
						generateDirListing();
						return;
					}
					LOG_DEBUG("Couldn't open file");
					error = HttpError(404, "Not Found");
					response = generateErrorResponse(error.message());
					return;
				}
				response += "200 OK\r\n";
				response += "Connection: close\r\n";
				response += "transfer-encoding: chunked\r\n\r\n";
			}
			else if (config->getDirectiveValue(path, header.getHost(), Config::Listing) == "on")
			{
				LOG_DEBUG("returning listing");
				generateDirListing();
				return;
			}
		}
		else
		{
			error = HttpError(404, "Not Found");
		}
	}
	else if (header.getMethod() == "POST" && config->isDirectiveAllowed(path, host, Config::AllowedMethods, "POST"))
	{
		if(header.getHeaders().count("transfer-encoding"))
		{
			if (header.getHeaders().find("transfer-encoding")->second == "chunked")
			{
				error = HttpError(501, header.getHeaders().find("transfer-encoding")->second + " encoding not supported")
				response = generateErrorResponse("")
			}
		}
		if (header.getHeaders().count(""))
		
		response += "200 OK";
	}
	else if (header.getMethod() == "DELETE")
	{
		if (!config->isDirectiveAllowed(path, host, Config::AllowedMethods, "DELETE"))
			error = HttpError(405, "Method Not Allowed");
		else if (remove(path.c_str()) != 0)
			error = HttpError(500, "Couldn't delete file");
		else {
			response += "200 OK\r\n\r\n";
			response += "<html><body><h1>File deleted</h1></body></html>\r\n";
		}
	}
	else
	{
		error = HttpError(405, "Method Not Allowed");
	}

	if (error.code() != 0)
		response = generateErrorResponse(error.message());
}

std::string HttpResponse::generateErrorResponse(HttpError &error) {
	std::string error_path = config->getErrorPage(error.code(), header.getPath(), header.getHeader("host"));
	response = "HTTP/1.1 ";
	std::stringstream errCode;
	errCode << error.code();
	if (error_path.empty())
	{
		std::string error_html = "<HTML><body><p><strong>";
		error_html += errCode.str();
		error_html += " </strong>";
		error_html += message;
		error_html += "</p></body>";

		response += errCode.str();
		response += " ";
		response += message + "\r\n";
		response += "Connection: close\r\n";
		response += "Content-Type: text/html\r\n";
		response += "Content-Length: ";
		std::stringstream errSize;
		errSize << error_html.size();
		response += errSize.str();
		response += "\r\n\r\n";
		response += error_html;
	}
	else
	{
		std::ifstream file(config->getFilePath(error_path, header.getHeader("host")).c_str());
		if (!file.is_open())
		{
			std::string error_html = "<HTML><body><p><strong>";
			error_html += "500";
			error_html += " </strong>";
			error_html += "Couldn't open error file";
			error_html += "</p></body>";

			response += errCode.str();
			response += " ";
			response += "Couldn't open error file\r\n";
			response += "Connection: close\r\n";
			response += "Content-Type: text/html\r\n";
			response += "Content-Length: ";
			std::stringstream errSize;
			errSize << error_html.size();
			response += errSize.str();
			response += "\r\n\r\n";
			response += error_html;
			return response;
		}
		response += errCode.str() + " ";
		response += message + "\r\n";
		response += "Connection: close\r\n";
		response += "Content-Type: text/html\r\n\r\n";
		response += std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	}
	LOG_DEBUG_WITH_TAG("Generated error response", "HttpResponse::generateErrorResponse");
	return response;
}

int HttpResponse::listDir(std::string dir, std::vector<fileInfo> &files)
{
	DIR *dp;
	struct dirent *dirp;
	if ((dp = opendir(dir.c_str())) == NULL)
	{
		LOG_ERROR("Couldn't open directory");
		return 1;
	}
	while ((dirp = readdir(dp)) != NULL)
	{
		struct stat fileStat;
		fileInfo fileInf;
		std::string filename = dir + dirp->d_name;
		LOG_DEBUG(dirp->d_name);
		if (stat(filename.c_str(), &fileStat) == -1)
		{
			LOG_ERROR("Couldn't get file stats");
		}
		fileInf.name = dirp->d_name;
		std::stringstream ss;
		ss << fileStat.st_size;
		fileInf.size = ss.str();
		fileInf.date = std::string(ctime(&fileStat.st_mtime));
		files.push_back(fileInf);
	}
	closedir(dp);
	return 0;
}

void HttpResponse::generateDirListing()
{
	std::string filePath = config->getFilePath(header.getPath(), header.getHeader("host"));
	std::vector<fileInfo> files;
	if (listDir(filePath, files))
	{
		LOG_DEBUG("Couldn't list directory");
		error = HttpError(500, "Internal Server Error");
		return ;
	}
	response += "200 OK\r\n";
	response += "Connection: close\r\n";
	response += "Content-Type: text/html\r\n";
	std::string listing = "<!DOCTYPE html>"
							"<html>"
							"<head>"
							"<style>"
							"#files {"
							"font-family: Arial, Helvetica, sans-serif;"
							"border-collapse: collapse;"
							"width: 100%;"
							"}"
							"#files td, #files th {"
							"border: 1px solid #ddd;"
							"padding: 8px;"
							"}"
							"#files tr:nth-child(even){background-color: #f2f2f2;}"
							"#files tr:hover {background-color: #ddd;}"
							"#files th {"
							"padding-top: 12px;"
							"padding-bottom: 12px;"
							"text-align: left;"
							"background-color: #04AA6D;"
							"color: white;"
							"}"
							"</style>"
							"</head>";
	listing += "<body><h2>Directory listing</h2><table id=\"files\">";
	listing += "<tr><th>Filename</th><th>Size (bytes)</th><th>Time of last data modification</th></tr>";
	for (size_t i = 0; i < files.size(); i++)
	{
		listing += "<tr><td><a href=\"";
		listing += files[i].name;
		listing += "\">";
		listing += files[i].name;
		listing += "</a></td><td>";

		listing += files[i].size;
		listing += "</td><td>";
		listing += files[i].date;
		listing += "</td></tr>";
	}
	listing += "</table></body></html>";
	response += "Content-Type: text/html\r\n";
	response += "Content-Length: ";
	std::stringstream ss;
	ss << listing.size();
	response += ss.str();
	response += "\r\n\r\n";
	response += listing;
}

HttpResponse::~HttpResponse() {
}

size_t HttpResponse::readBuffer(const char *buffer) {
	(void)buffer;
	if (error.code() != 0)
		return 0;
	return 0;
}

void HttpResponse::write() {
	if (isFinished)
	{
		LOG_INFO("HttpResponse::write isFinished");
		return;
	}

	if (header.getMethod() == "GET" && !error.code()) {
		if (response.size())
		{
			LOG_DEBUG("HttpResponse sending response buffer");
			// sending headers
			ssize_t sentBytes =  send(fds, response.c_str(), response.size(), 0);
			if (sentBytes >= 0)
				response = response.substr(sentBytes);
		} else if (getFile.is_open()) {
			// sending file
			LOG_DEBUG("HttpResponse reading chunk from file");
			getFile.read(chunkedBuffer, 1023);
			if (getFile.gcount() == 0)
			{
				getFile.close();
				response += "0\r\n\r\n";
				return;
			}
			size_t readBytes = getFile.gcount();
			chunkedBuffer[readBytes] = '\0';
			std::stringstream ss;
			ss << std::hex << readBytes;
			response += ss.str();
			response += "\r\n";
			response += std::string(chunkedBuffer, readBytes);
			response += "\r\n";
		} else {
			isFinished = true;
		}
	} else {
		if (response.size() > 0) {
			ssize_t sentBytes =  send(fds, response.c_str(), response.size(), 0);
			response = response.substr(sentBytes);
		}
		else {
			isFinished = true;
		}
	}
}

bool HttpResponse::finished() {
	return isFinished;
}
