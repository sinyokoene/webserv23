#include "webserv.hpp"
#include "Server.hpp"

void	addFlag(int fd, int flag)
{
	int fl = fcntl(fd, F_GETFL, 0);
	if (fl == -1 || fcntl(fd, F_SETFL, fl | flag) == -1)
	{
		perror("addFlag failed");
		return;
	}
}

bool	isDir(std::string path)
{
	struct stat		st;

	if (stat(path.c_str(), &st) == -1)
	{
		return false;
	}
	return S_ISDIR(st.st_mode);
}

std::map<std::string, t_location>::iterator	smartFindLocation(Server& server, std::string fullPath)
{
	uint64_t	pos = fullPath.find_last_of("/");
	std::string	loc = fullPath.substr(0, pos + 1);

	return server._locations.find(loc);
}

std::string	findSegment(std::string req, std::string start, std::string end, uint64_t idx)
{
	size_t p1 = req.find(start);
	if (p1 == req.npos)
		return "";
	idx += p1;

	size_t p2 = req.find(end, idx);
	if (p2 == req.npos)
		return "";
	
	return req.substr(idx, p2 - idx);
}

std::string	getLocalTime(std::string fmt)
{
	auto				now = std::chrono::system_clock::now();
	std::time_t			t = std::chrono::system_clock::to_time_t(now);
	std::tm				tm = *std::localtime(&t);
	std::ostringstream	ss;

	ss << std::put_time(&tm, fmt.c_str());
	return ss.str();
}

int64_t	getTimeMS()
{
	using namespace std::chrono;

	return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

bool	timer(int64_t dur)
{
	static int64_t end;
	int64_t now = getTimeMS();

	if (dur != 0)
	{
		end = now + dur;
		return false;
	}
	return end <= now;
}

std::string	findKeyword(const std::string& src, const std::string& key, bool err)
{
	uint64_t s = src.find(key);
	uint64_t e = src.find_first_of("\n#", s);
	if ((s == src.npos || e == src.npos))
	{
		if (err == true)
		{
			std::cerr << "Couldn't find " << key << std::endl;
			throw Server::ParseError();
		}
		return "";
	}
	s += key.length();
	s = src.find_first_not_of(" \t", s);
	while (e > 0 && (src[e - 1] == ' ' || src[e - 1] == '\t'))
		e--;
	if (s > e)
	{
		std::cerr << "findKeyword: start > end\ncheck if " << key << " has a value\n";
		throw Server::ParseError();
	}
	return src.substr(s, e - s);
}