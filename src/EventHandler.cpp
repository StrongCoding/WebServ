#include "EventHandler.hpp"
#include "Client.hpp"

EventHandler EventHandler::_instance;

EventHandler &EventHandler::getInstance() {
	return _instance;
}

EventHandler::EventHandler() :
		_epollFd(0),
		isRunning(false) {}

EventHandler::~EventHandler() {
	for (std::list<EventsData *>::iterator it = _eventDataList.begin(); it != _eventDataList.end(); it++) {
		delete *it;
	}
	_eventDataList.clear();
}

// Starts the event main loop, can only be called once
void EventHandler::start() {
	if (getInstance().isRunning) {
		return;
	}
	getInstance().isRunning = true;
	SocketHandling sockets(Config::getInstance()->getServerBlocks());
	_epollFd = sockets.getEpollFd();
	_listeningSockets = sockets.getOpenFds();
	struct epoll_event events[MAX_EVENTS];
	int epollTriggerCount;

	signal(SIGPIPE, SIG_IGN);
	while (true) {
		epollTriggerCount = epoll_wait(_epollFd, events, MAX_EVENTS, -1);
		if (epollTriggerCount == -1) {
			LOG_ERROR("epoll_wait failed.");
			continue;
		}
		for (int n = 0; n < epollTriggerCount; ++n) {
			_currentEvent = static_cast<EventsData *>(events[n].data.ptr);
			EventType type = _currentEvent->eventType;
			if (type == LISTENING) {
				acceptNewClient(_currentEvent);
				continue;
			} else if (type == CLIENT || type == CGI) {
				Client *client = static_cast<Client *>(_currentEvent->objectPointer);
				client->process(events[n].events);
			}
		}
		removeInactiveClients();
	}
}

EventHandler::EventHandler(EventHandler const &other) {
	(void)other;
}

EventHandler &EventHandler::operator=(EventHandler const &other) {
	(void)other;
	return *this;
}

// Process the cleanup list and remove the clients from the epoll list
void EventHandler::processCleanUpList() {
	for (std::list<EventsData *>::iterator itCleanUp = _cleanUpList.begin(); itCleanUp != _cleanUpList.end(); itCleanUp++) {
		if ((*itCleanUp)->eventType == CLIENT) {
			delete static_cast<Client *>((*itCleanUp)->objectPointer);
		}
	}
	for (std::list<EventsData *>::iterator itCleanUp = _cleanUpList.begin(); itCleanUp != _cleanUpList.end(); itCleanUp++) {
		unregisterEvent(*itCleanUp);
	}
	_cleanUpList.clear();
}

// Accept and add new client to epoll and client list
void EventHandler::acceptNewClient(EventsData *eventData) {
	int newConnectionFd;
	struct sockaddr_in addr;
	socklen_t addrlen = sizeof(addr);
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	newConnectionFd = accept(eventData->fd, (struct sockaddr *)&addr, &addrlen);
	if (newConnectionFd == -1) {
		LOG_ERROR("EventHandler: accept failed.");
		return;
	}
	Client *newClient = NULL;
	try {
		std::string ip = ft_inet_ntop(AF_INET, &addr.sin_addr);
		newClient = new Client(newConnectionFd, ip);
		registerEvent(newConnectionFd, CLIENT, newClient);
		LOG_DEBUG("New client connection");
	} catch (...) {
		delete newClient;
		close(newConnectionFd);
	}
}

EventsData *EventHandler::createNewEvent(int fd, EventType type, Client *client) {
	EventsData *eventData = new EventsData;
	eventData->fd = fd;
	eventData->eventType = type;
	if (type == CLIENT || type == CGI) {
		eventData->objectPointer = client;
	} else {
		eventData->objectPointer = NULL;
	}
	return eventData;
}

// int EventHandler::getEpollFd() const {
// 	return _epollFd;
// }

int EventHandler::registerEvent(int fd, EventType type, Client *client) {
	struct epoll_event ev;
	ev.events = EPOLLIN | EPOLLOUT;
	ev.data.ptr = createNewEvent(fd, type, client);
	_eventDataList.push_back(static_cast<EventsData *>(ev.data.ptr));
	if (epoll_ctl(_epollFd, EPOLL_CTL_ADD, fd, &ev) == -1) {
		LOG_ERROR("RegisterEvent: epoll ADD failed.");
		return -1;
	}
	return 0;
}

void EventHandler::unregisterEvent(int fd) {
	if (epoll_ctl(_epollFd, EPOLL_CTL_DEL, fd, NULL) == -1) {
		std::cerr << "Fd: " << fd << std::endl; // TODO: DELETE
		LOG_ERROR("UnregisterEvent: epoll DEL failed.");
	}
	for (std::list<EventsData *>::iterator it = _eventDataList.begin(); it != _eventDataList.end(); it++) {
		if ((*it)->fd == fd) {
			LOG_DEBUG_WITH_TAG("Unregistered something with fd", "EventHandler");
			delete *it;
			_eventDataList.erase(it);
			break;
		}
	}
}

void EventHandler::unregisterEvent(EventsData *eventData) {
	if (epoll_ctl(_epollFd, EPOLL_CTL_DEL, eventData->fd, NULL) == -1) {
		perror("epoll_ctl");
		std::cerr << "Fd: " << eventData->fd << std::endl; // TODO: DELETE
		LOG_ERROR("UnregisterEvent: epoll DEL failed.");
		perror("epoll_ctl");
	}
	for (std::list<EventsData *>::iterator it = _eventDataList.begin(); it != _eventDataList.end(); it++) {
		if (*it == eventData) {
			LOG_DEBUG_WITH_TAG("Unregistered something with pointer", "EventHandler");
			close(eventData->fd);
			delete *it;
			_eventDataList.erase(it);
			break;
		}
	}
}

void EventHandler::addToCleanUpList(int fd) {
	for (std::list<EventsData *>::iterator it = _eventDataList.begin(); it != _eventDataList.end(); it++) {
		if ((*it)->fd == fd) {
			_cleanUpList.push_back(*it);
			break;
		}
	}
}

void EventHandler::addToCleanUpList(EventsData *eventData) {
	_cleanUpList.push_back(eventData);
}

void EventHandler::removeInactiveClients() {
	for (std::list<EventsData *>::iterator it = _eventDataList.begin(); it != _eventDataList.end(); it++) {
		EventsData *eventData = *it;
		if (eventData->eventType == CLIENT) {
			Client *client = static_cast<Client *>(eventData->objectPointer);
			if (client->canBeDeleted()) {
				LOG_DEBUG("Client can be deleted");
				_cleanUpList.push_back(eventData);
				continue;
			} else if (client->isTimeouted()) {
				LOG_DEBUG("Client timeout");
				_cleanUpList.push_back(*it);
			}
		}
	}
	processCleanUpList();
}

std::string EventHandler::ft_inet_ntop(int af, const void *src) {
	if (af == AF_INET) {
		const unsigned char *bytes = (const unsigned char *)src;
		std::ostringstream oss;
		oss << static_cast<int>(bytes[0]) << ".";
		oss << static_cast<int>(bytes[1]) << ".";
		oss << static_cast<int>(bytes[2]) << ".";
		oss << static_cast<int>(bytes[3]);
		return oss.str();
	}
	return std::string();
}