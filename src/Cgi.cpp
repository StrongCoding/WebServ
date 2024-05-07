#include "Cgi.hpp"
#include <cstdlib>
#include <cstring>

char *dupString(const std::string &str) {
	char *cstr = new char[str.length() + 1];
	std::strcpy(cstr, str.c_str());
	return cstr;
}

std::string Cgi::toString(size_t number) {
	std::stringstream result;

	result << number;
	return result.str();
}

std::vector<std::string> Cgi::createEnviromentVariables() {
	std::vector<std::string> result;
	std::string contentType = _headerObject->getHeader("content-type");

	result.push_back("AUTH_TYPE=");
	result.push_back("CONTENT_LENGTH=" + toString(_bodyLength));
	if (!contentType.empty()) {
		result.push_back("CONTENT_TYPE=" + contentType);
	}
	result.push_back("GATEWAY_INTERFACE=CGI/1.1");
	result.push_back("PATH_INFO=" + Config::getInstance()->getFilePath(_headerObject->getPath(), "localhost"));
	result.push_back("PATH_TRANSLATED=");
	result.push_back("QUERY_STRING=" + _headerObject->getQuery());
	result.push_back("REMOTE_ADDR=" + _clientIp);
	result.push_back("REMOTE_HOST=" + _clientIp);
	// result.push_back("REMOTE_IDENT=");
	result.push_back("REQUEST_METHOD=" + _headerObject->getMethod());
	result.push_back("SCRIPT_NAME=" + _headerObject->getPath()); // FIXME: Maybe wrong when: /path/to/script.py/param1/param2
	result.push_back("SERVER_NAME=" + _headerObject->getHeader("host"));
	// result.push_back("SERVER_PORT=" + _headerObject->getPort()); // TODO: Change to actual server port
	result.push_back("SERVER_PROTOCOL=HTTP/1.1");
	result.push_back("SERVER_SOFTWARE=WebServ/1.0");

	for (std::map<std::string, std::string>::const_iterator it = _headerObject->getHeaders().begin(); it != _headerObject->getHeaders().end(); ++it) {
		std::string key = it->first;
		std::transform(key.begin(), key.end(), key.begin(), ::toupper);
		std::replace(key.begin(), key.end(), '-', '_');
		result.push_back("HTTP_" + key + "=" + it->second);
	}

	return result;
}

char **Cgi::createArguments() {
	char **argv = new char *[3];
	argv[0] = strdup("/bin/python3");
	argv[1] = strdup("overflow.py");
	argv[2] = NULL; // The environment list must be NULL-terminated

	return argv;
}

Cgi::Cgi(const std::string &bodyBuffer, HttpHeader *headerObject, std::string clientIp, int fd) :
		_currentBufferSize(0),
		_timeCreated(0),
		_isFinished(false),
		_errorCode(0),
		_bodyBuffer(bodyBuffer),
		_headerObject(headerObject),
		_clientIp(clientIp),
		_currentState(READING_BODY),
		_fd(fd) {
	_sockets[0] = -1;
	_sockets[1] = -1;
	_bodyLength = bodyBuffer.size();
	executeCgi();
}

Cgi::~Cgi() {
	if (_sockets[0] != -1) {
		close(_sockets[0]);
	}
}

bool Cgi::isFinished() const {
	return _isFinished;
}

int Cgi::getErrorCode() const {
	return _errorCode;
}

void Cgi::executeCgi() {
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, _sockets) < 0) {
		LOG_ERROR_WITH_TAG("Failed to create socket pair", "CGI");
		_isFinished = true;
		_errorCode = 500;
		return;
	}

	LOG_DEBUG("CGI EXECUTING");
	// Add the socket to epoll event list and create an EventData object for it
	// try {
	// if (client->getEventHandler()->registerEvent(_sockets[0], CGI, client) < 0) {
	// 	LOG_ERROR_WITH_TAG("Failed to add socket to epoll", "CGI");
	// 	_isFinished = true;
	// 	_errorCode = 500;
	// 	return;
	// }
	// EventsData *eventData = client->getEventHandler()->createNewEvent(_sockets[0], CGI, client);
	// client->getEventHandler()->addEventToList(eventData);
	// epoll_event ev;
	// ev.events = EPOLLIN | EPOLLOUT;
	// ev.data.ptr = eventData;
	// if (epoll_ctl(client->getEventHandler()->getEpollFd(), EPOLL_CTL_ADD, _sockets[0], &ev) < 0) {
	// 	LOG_ERROR_WITH_TAG("Failed to add socket to epoll", "CGI");
	// 	_isFinished = true;
	// 	_errorCode = 500;
	// 	return;
	// }
	// } catch (std::bad_alloc &e) {
	// 	LOG_ERROR_WITH_TAG("Failed to allocate memory", "CGI");
	// 	_isFinished = true;
	// 	_errorCode = 500;
	// 	return;
	// }

	// Update the last modified time
	_timeCreated = std::time(0);

	// Fork the process
	pid_t pid = fork();
	if (pid < 0) {
		LOG_ERROR_WITH_TAG("Failed to fork", "CGI");
		_isFinished = true;
		_errorCode = 500;
		return;
	} else if (pid != 0) { // Parent process
		close(_sockets[1]); // Close child's end of the socket pair

	} else { // Child process
		executeChild(_headerObject);
	}
	// close(_sockets[0]);
}

int Cgi::executeChild(const HttpHeader *headerObject) {
	close(_sockets[0]); // Close parent's end of the socket pair
	dup2(_sockets[1], STDIN_FILENO);
	dup2(_sockets[1], STDOUT_FILENO);
	// close(_sockets[1]); // Close child's end of the socket pair

	// TODO: Implement change directory
	// if (chdir(Config::getInstance().getDirPath()) < 0) {
	// 	LOG_ERROR_WITH_TAG("Failed to change directory", "CGI");
	// 	std::exit(255);
	// }

	if (chdir("cgi/") < 0) {
		LOG_ERROR_WITH_TAG("Failed to change directory", "CGI");
		std::exit(255);
	}

	// Set the enviroment variables
	// char **envp = createEnvironment(headerObject);
	char **envp = NULL;
	(void)headerObject;
	char **argv = createArguments();

	execve(argv[0], argv, envp);
	LOG_ERROR_WITH_TAG("Failed to execve CGI", "CGI");
	perror("execve failed:"); // TODO: DELETE DEBUG

	std::exit(255);

	return 0;
}


void Cgi::readBody() {
	if (_headerObject->getMethod() == "POST") {
		if (_headerObject->getHeader("transfer-encoding").find("chunked")) {
			if (!decodeChunkedBuffer(_bodyBuffer, _sendToChildBuffer)) {
				_errorCode = 400;
				_currentState = SENDING_RESPONSE;
			}
			// get chunked stuff;
		} else {
			if (_bodyBuffer.size()) {
				_sendToChildBuffer += _bodyBuffer;
				_bodyBuffer.clear();
				if (_bodyLength == _sendToChildBuffer.size()) {
					_currentState = CREATE_CHILD;
					return;
				}
			}
			char buffer[BUFFER_SIZE + 1];
			ssize_t readSize = read(_fd, buffer, BUFFER_SIZE);
			if (readSize > 0) {
				buffer[readSize] = 0;
				_sendToChildBuffer += buffer;
				if (_bodyLength && _sendToChildBuffer.size() > _bodyLength) {
					_errorCode = 400;
					_currentState = SENDING_RESPONSE;
				}
				else if(_bodyLength == _sendToChildBuffer.size()) {
				_currentState = CREATE_CHILD;
				}
			}
			else if (readSize == -1) {
				_currentState = FINISHED;
			}
			else {
				_errorCode = 400;
				_currentState = SENDING_RESPONSE;
			} 
		}
	}
	else if (_headerObject->getMethod() == "GET") {
		_currentState = CREATE_CHILD;
		return;
	}
}

void Cgi::process() {
	switch (_currentState) {
		case READING_BODY:
			readBody();
			break;
		case CREATE_CHILD:
			break;
		case WAITING_FOR_CHILD:
			break;
		case SENDING_RESPONSE:
			break;
		case FINISHED:
			break;
	}
}