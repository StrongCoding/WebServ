#ifndef UTILS_HPP
# define UTILS_HPP
# include <string>
# include <sys/stat.h>
# include <sstream>

namespace Utils {
	bool isFolder(const std::string &path);
	bool isFile(const std::string &path);
	std::string toString(size_t number);
	std::string toString(int number);
	int stringToNumber(const std::string& s);
}

#endif